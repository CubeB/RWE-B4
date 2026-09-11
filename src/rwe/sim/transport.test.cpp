#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/cob/CobPosition.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 64, int height = 64)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
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
            d.canLoad = true;
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

        void push(CobScript& script, OpCode op)
        {
            script.instructions.push_back(static_cast<uint32_t>(op));
        }

        void push(CobScript& script, uint32_t operand)
        {
            script.instructions.push_back(operand);
        }

        void beginFunction(CobScript& script, const std::string& name)
        {
            script.functions.push_back(CobFunctionInfo{name, static_cast<unsigned int>(script.instructions.size())});
        }

        void endFunction(CobScript& script)
        {
            push(script, OpCode::PUSH_CONSTANT);
            push(script, 0u);
            push(script, OpCode::RETURN);
        }

        constexpr unsigned int StaticQueried = 0;
        constexpr unsigned int StaticHookDrop = 1;

        /**
         * An Atlas in miniature. `QueryTransport` files the fact that it was
         * asked and hands back piece 0 -- the Atlas hands back `link` -- and
         * `BeginTransport` files the height it was told to lower the hook by,
         * which is all the shipped script does with it (§39: one move-now to
         * y = -h). A test can then read both back out of the statics and see
         * when each happened relative to the attach.
         */
        std::shared_ptr<CobScript> makeAirTransportScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 2;
            script->pieces.push_back("base");

            beginFunction(*script, "QueryTransport");
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, 1u);
            push(*script, OpCode::POP_STATIC);
            push(*script, StaticQueried);
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, 0u); // the piece the cargo hangs from
            push(*script, OpCode::POP_LOCAL_VAR);
            push(*script, 0u);
            endFunction(*script);

            beginFunction(*script, "BeginTransport");
            push(*script, OpCode::PUSH_LOCAL_VAR);
            push(*script, 0u);
            push(*script, OpCode::POP_STATIC);
            push(*script, StaticHookDrop);
            endFunction(*script);

            return script;
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
        auto script = makeEmptyCobScript({"base"});
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
                // Rides on deck: same spot on the map, a little above the ship.
                const auto& kbotPos = sim.getUnitState(kbotId).position;
                const auto& shipPos = sim.getUnitState(transportId).position;
                REQUIRE(kbotPos.x == shipPos.x);
                REQUIRE(kbotPos.z == shipPos.z);
                REQUIRE(kbotPos.y > shipPos.y);
                REQUIRE(kbotPos.y < shipPos.y + 16_ss);
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

        SECTION("a unit that says CantBeTransported is refused however much room there is")
        {
            // 0x489AA3 is the first thing the load predicate asks, before it
            // has even looked at the transport, so a unit that names the key
            // is refused by everything. The Sumo is the one unit in the
            // shipped data that does; nothing about it is too big or too
            // heavy for the transports, which is what makes the flag the only
            // thing standing between it and a free ride.
            sim.unitDefinitions.at("kbot").cantBeTransported = true;
            sim.getUnitState(transportId).orders.push_back(LoadOrder(kbotId));
            sim.tick();
            REQUIRE(sim.getUnitState(transportId).orders.empty());
            REQUIRE_FALSE(sim.getUnitState(kbotId).carriedBy.has_value());
        }

        SECTION("only transports respond to load orders")
        {
            sim.getUnitState(kbotId).orders.push_back(LoadOrder(transportId));
            sim.tick();
            REQUIRE(sim.getUnitState(kbotId).orders.empty());
            REQUIRE_FALSE(sim.getUnitState(transportId).carriedBy.has_value());
        }

        SECTION("a unit put aboard forgets where it was going")
        {
            // Walking somewhere of its own accord when the transport picks it up.
            sim.getUnitState(kbotId).orders.push_back(createMoveOrder(SimVector(400_ss, 0_ss, 400_ss)));
            sim.getUnitState(transportId).orders.push_back(LoadOrder(kbotId));
            REQUIRE(tickUntil(sim, 600, [&] { return sim.getUnitState(kbotId).carriedBy.has_value(); }));
            REQUIRE(sim.getUnitState(kbotId).orders.empty());

            // Set it down somewhere else: it stays put instead of resuming the old march.
            auto destination = SimVector(-200_ss, 0_ss, -200_ss);
            sim.getUnitState(transportId).orders.push_back(UnloadOrder(destination));
            REQUIRE(tickUntil(sim, 900, [&] { return !sim.getUnitState(kbotId).carriedBy.has_value(); }));
            REQUIRE(sim.getUnitState(kbotId).orders.empty());
            auto restingPlace = sim.getUnitState(kbotId).position;
            for (int i = 0; i < 60; ++i)
            {
                sim.tick();
            }
            REQUIRE(sim.getUnitState(kbotId).position.distanceSquared(restingPlace) == 0_ss);
        }

        SECTION("cargo cannot be selected or given orders of its own")
        {
            sim.getUnitState(transportId).orders.push_back(LoadOrder(kbotId));
            REQUIRE(tickUntil(sim, 600, [&] { return sim.getUnitState(kbotId).carriedBy.has_value(); }));
            const auto& kbot = sim.getUnitState(kbotId);
            REQUIRE_FALSE(kbot.isSelectableBy(sim.unitDefinitions.at("kbot"), player));
            // Back on the ground it is a unit again.
            sim.unloadUnitFromTransport(transportId, kbotId, SimVector(100_ss, 0_ss, 100_ss));
            REQUIRE(sim.getUnitState(kbotId).isSelectableBy(sim.unitDefinitions.at("kbot"), player));
        }
    }

    TEST_CASE("loading is asymmetric: the transport loads the unit, never the reverse", "[transport]")
    {
        // The roadmap once listed "units ordering themselves aboard (select
        // units, click transport)" as work to do. The original has no such
        // order -- all five CanLoadUnit (0x489A90) call sites pass the
        // ordering unit as the transport -- so this pins the asymmetry the
        // sim already has, so nobody adds a passenger-side path later
        // thinking it is TA.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "hauler");
        sim.unitDefinitions["transport"] = makeTransportDef();
        sim.unitDefinitions["kbot"] = makeMobileDef(2u);
        registerModel(sim, "model");

        auto transportId = spawnUnit(sim, "transport", player, SimVector(-200_ss, 0_ss, 0_ss), script);
        auto kbotId = spawnUnit(sim, "kbot", player, SimVector(0_ss, 0_ss, 0_ss), script);

        REQUIRE(sim.canLoadUnitIntoTransport(transportId, kbotId));
        REQUIRE_FALSE(sim.canLoadUnitIntoTransport(kbotId, transportId));
    }

    TEST_CASE("a ship sends the unit it is collecting towards itself", "[transport]")
    {
        // A ship cannot come ashore, so the unit walks down to meet it. The
        // bug this guards against had the unit setting off in the opposite
        // direction, then further away again each time it arrived.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "hauler");
        auto shipDef = makeTransportDef();
        shipDef.floater = true;
        sim.unitDefinitions["ship"] = shipDef;
        sim.unitDefinitions["kbot"] = makeMobileDef(2u);
        registerModel(sim, "model");

        auto shipId = spawnUnit(sim, "ship", player, SimVector(-400_ss, 0_ss, 0_ss), script);
        auto kbotId = spawnUnit(sim, "kbot", player, SimVector(0_ss, 0_ss, 0_ss), script);
        auto kbotStart = sim.getUnitState(kbotId).position;

        sim.getUnitState(shipId).orders.push_back(LoadOrder(kbotId));
        REQUIRE(tickUntil(sim, 60, [&] { return !sim.getUnitState(kbotId).orders.empty(); }));

        auto move = std::get_if<MoveOrder>(&sim.getUnitState(kbotId).orders.front());
        REQUIRE(move != nullptr);

        auto shipPosition = sim.getUnitState(shipId).position;
        auto distanceFromStart = shipPosition.distanceSquared(kbotStart);
        auto distanceFromDestination = shipPosition.distanceSquared(move->destination);
        // The meeting point is nearer the ship than the unit was.
        REQUIRE(distanceFromDestination < distanceFromStart);
        // And it is on the ship's side of the unit, not the far side.
        REQUIRE(move->destination.x < kbotStart.x);
    }

    TEST_CASE("one unload order sets down one unit", "[transport]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "hauler");
        auto transportDef = makeTransportDef();
        transportDef.transportCapacity = 4;
        sim.unitDefinitions["transport"] = transportDef;
        sim.unitDefinitions["kbot"] = makeMobileDef(2u);
        registerModel(sim, "model");

        auto transportId = spawnUnit(sim, "transport", player, SimVector(-200_ss, 0_ss, 0_ss), script);
        auto firstId = spawnUnit(sim, "kbot", player, SimVector(0_ss, 0_ss, 0_ss), script);
        auto secondId = spawnUnit(sim, "kbot", player, SimVector(0_ss, 0_ss, 64_ss), script);

        sim.getUnitState(transportId).orders.push_back(LoadOrder(firstId));
        sim.getUnitState(transportId).orders.push_back(LoadOrder(secondId));
        REQUIRE(tickUntil(sim, 1200, [&] { return sim.getUnitState(transportId).carriedUnits.size() == 2; }));

        auto destination = SimVector(200_ss, 0_ss, 200_ss);
        sim.getUnitState(transportId).orders.push_back(UnloadOrder(destination));
        REQUIRE(tickUntil(sim, 900, [&] { return sim.getUnitState(transportId).carriedUnits.size() == 1; }));

        // The order is finished and the second unit is still aboard.
        REQUIRE(tickUntil(sim, 120, [&] { return sim.getUnitState(transportId).orders.empty(); }));
        REQUIRE(sim.getUnitState(transportId).carriedUnits == std::vector<UnitId>{secondId});
        REQUIRE(sim.getUnitState(secondId).carriedBy.has_value());

        // A second order sets down the other one.
        sim.getUnitState(transportId).orders.push_back(UnloadOrder(destination));
        REQUIRE(tickUntil(sim, 900, [&] { return sim.getUnitState(transportId).carriedUnits.empty(); }));
    }

    TEST_CASE("who may ride: the original's load predicate", "[transport]")
    {
        // 0x489A90, rule by rule. The terrain floor is at zero with the sea
        // at twenty, so a unit at y=15 with the ten-unit test model pokes
        // its top above the water and one at y=5 is fully under.
        auto script = makeEmptyCobScript({"base"});
        Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
        GameSimulation sim(MapTerrain(std::move(heights), 20_ss), 0u, 0, 0);
        auto player = addPlayer(sim, "us");
        registerModel(sim, "model");

        sim.unitDefinitions["seatransport"] = makeTransportDef();
        auto airDef = makeTransportDef();
        airDef.canFly = true;
        airDef.transportCapacity = 5; // the Atlas says 5; the engine says 1
        sim.unitDefinitions["airtransport"] = airDef;
        sim.unitDefinitions["kbot"] = makeMobileDef(2u);
        auto shipDef = makeMobileDef(2u);
        shipDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 10u, 255u};
        sim.unitDefinitions["ship"] = shipDef;

        auto transportId = spawnUnit(sim, "seatransport", player, SimVector(100_ss, 15_ss, 100_ss), script);
        auto airId = spawnUnit(sim, "airtransport", player, SimVector(300_ss, 60_ss, 100_ss), script);
        auto kbotId = spawnUnit(sim, "kbot", player, SimVector(150_ss, 15_ss, 100_ss), script);

        SECTION("a plain mobile unit may board anything")
        {
            REQUIRE(sim.canLoadUnitIntoTransport(transportId, kbotId));
            REQUIRE(sim.canLoadUnitIntoTransport(airId, kbotId));
        }

        SECTION("a ship is refused by a sea transport but not on account of the water rule alone")
        {
            // minwaterdepth > 0 is the sea/hover refusal; the air transport
            // in the original refuses ships through the footprint gate
            // instead, so a narrow test ship is fair game for it.
            auto shipId = spawnUnit(sim, "ship", player, SimVector(150_ss, 15_ss, 150_ss), script);
            REQUIRE_FALSE(sim.canLoadUnitIntoTransport(transportId, shipId));
            REQUIRE(sim.canLoadUnitIntoTransport(airId, shipId));
        }

        SECTION("nothing lifts a submerged unit")
        {
            sim.getUnitState(kbotId).position.y = 5_ss;
            REQUIRE_FALSE(sim.canLoadUnitIntoTransport(transportId, kbotId));
            REQUIRE_FALSE(sim.canLoadUnitIntoTransport(airId, kbotId));
        }

        SECTION("an airborne unit may not be picked up, a landed one may")
        {
            sim.getUnitState(kbotId).physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            REQUIRE_FALSE(sim.canLoadUnitIntoTransport(transportId, kbotId));
            sim.getUnitState(kbotId).physics = UnitPhysicsInfoGround{};
            REQUIRE(sim.canLoadUnitIntoTransport(transportId, kbotId));
        }

        SECTION("an air transport carries one, whatever its FBI says")
        {
            auto secondId = spawnUnit(sim, "kbot", player, SimVector(170_ss, 15_ss, 100_ss), script);
            REQUIRE(sim.canLoadUnitIntoTransport(airId, kbotId));
            REQUIRE(sim.loadUnitIntoTransport(airId, kbotId, std::string()));
            REQUIRE_FALSE(sim.canLoadUnitIntoTransport(airId, secondId));
        }

        SECTION("a unit carrying cargo can never itself be loaded")
        {
            auto secondId = spawnUnit(sim, "seatransport", player, SimVector(170_ss, 15_ss, 100_ss), script);
            REQUIRE(sim.loadUnitIntoTransport(secondId, kbotId, std::string()));
            REQUIRE_FALSE(sim.canLoadUnitIntoTransport(transportId, secondId));
        }

        SECTION("cantbetransported refuses every transport there is")
        {
            sim.unitDefinitions["kbot"].cantBeTransported = true;
            REQUIRE_FALSE(sim.canLoadUnitIntoTransport(transportId, kbotId));
            REQUIRE_FALSE(sim.canLoadUnitIntoTransport(airId, kbotId));
        }

        SECTION("the size gate is footprint X against transportsize")
        {
            sim.unitDefinitions["kbot"].movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 2u, 255u, 255u, 0u, 0u};
            REQUIRE_FALSE(sim.canLoadUnitIntoTransport(transportId, kbotId));
        }
    }

    TEST_CASE("an air transport lowers its hook before it descends", "[transport]")
    {
        // VTOL_Pickup states 2 to 4 (§36, §39, §103). The Atlas arrives over
        // the cargo at cruise altitude, asks the script for the hook piece
        // (QueryTransport), tells the script how far to drop it
        // (BeginTransport(height(u))) and only then descends -- to
        // height(u), the altitude that leaves the lowered hook on the unit.
        // RWE used to descend first, attach, and animate afterwards.
        auto script = makeAirTransportScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "hauler");

        auto airDef = makeTransportDef();
        airDef.canFly = true;
        airDef.cruiseAltitude = 200_ss;
        sim.unitDefinitions["airtransport"] = airDef;
        sim.unitDefinitions["kbot"] = makeMobileDef(2u);
        registerModel(sim, "model");
        const auto cargoHeight = sim.unitModelDefinitions.at("model").height;

        auto airId = spawnUnit(sim, "airtransport", player, SimVector(-200_ss, 200_ss, 0_ss), script);
        sim.getUnitState(airId).physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
        sim.flyingUnitsSet.insert(airId);
        auto kbotId = spawnUnit(sim, "kbot", player, SimVector(0_ss, 0_ss, 0_ss), script);
        auto kbotGroundY = sim.getUnitState(kbotId).position.y;

        sim.getUnitState(airId).orders.push_back(LoadOrder(kbotId));

        // Sampled before each tick, so what these hold when the attach lands
        // is the state at the end of the tick *before* it.
        bool queriedBeforeAttach = false;
        bool hookDroppedBeforeAttach = false;
        std::optional<SimScalar> attachAltitude;
        for (int i = 0; i < 1800; ++i)
        {
            const auto& env = *sim.getUnitState(airId).cobEnvironment;
            auto queried = env._statics[StaticQueried] != 0;
            auto hookDropped = env._statics[StaticHookDrop] != 0;
            sim.tick();
            if (sim.getUnitState(kbotId).carriedBy.has_value())
            {
                queriedBeforeAttach = queried;
                hookDroppedBeforeAttach = hookDropped;
                attachAltitude = sim.getUnitState(airId).position.y;
                break;
            }
        }

        REQUIRE(attachAltitude.has_value());
        REQUIRE(*sim.getUnitState(kbotId).carriedBy == airId);

        // Both calls are behind it by the time it takes the unit aboard.
        REQUIRE(queriedBeforeAttach);
        REQUIRE(hookDroppedBeforeAttach);

        // And the hook was dropped by the cargo's own model height, in the
        // 16.16 units a script reads from UNIT_HEIGHT -- the original hands
        // over the whole dword at targetdef+0x16E, not its integer part.
        REQUIRE(sim.getUnitState(airId).cobEnvironment->_statics[StaticHookDrop] == CobPosition::fromFloat(simScalarToFloat(cargoHeight)).value);

        // It attached at the cargo-height altitude, not the cruise altitude
        // it flew in at. The arrival tolerance for the hover is 24 units.
        REQUIRE(*attachAltitude <= kbotGroundY + cargoHeight + 24_ss);
        REQUIRE(*attachAltitude < airDef.cruiseAltitude);

        // The piece the script named is the piece the cargo hangs from.
        REQUIRE(sim.getUnitState(kbotId).carriedPiece == "base");
    }
}
