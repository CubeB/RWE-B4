#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/game/idle_builders.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <algorithm>
#include <memory>
#include <type_traits>
#include <optional>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeIdleBuilderTerrain(int width, int height)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addIdleBuilderPlayer(GameSimulation& sim, const std::string& name)
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

        /** The ARM Construction Bot, out of ARMCON.FBI: a builder that walks. */
        UnitDefinition makeConstructionBotDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.builder = true;
            d.buildDistance = 32_ss;
            d.maxVelocity = 5.5_ssf;
            d.acceleration = 0.1444_ssf;
            d.brakeRate = 0.1444_ssf;
            d.turnRate = 242_ss;
            d.workerTimePerTick = 2u;
            d.maxHitPoints = 135;
            d.sightDistance = 364u;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        /** The ARM Advanced Factory: a builder that is a building. */
        UnitDefinition makeFactoryDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            // A building, not a vehicle. isMobile false is what makes this a
            // factory rather than a construction unit, and it is exactly what
            // the count's own test turns on -- a definition that calls itself a
            // builder but reports itself mobile is a construction unit, and one
            // that does not is a factory nothing can send to a job. That is
            // also the branch of tryAddUnit that wants a yardmap, because a
            // building blocks by its shape rather than by a movement class.
            d.isMobile = false;
            d.canMove = false;
            d.builder = true;
            d.buildDistance = 64_ss;
            d.workerTimePerTick = 2u;
            d.maxHitPoints = 1155;
            d.sightDistance = 455u;
            d.buildTime = 0u;
            d.yardMap = Grid<YardMapCell>(4, 4, YardMapCell::Ground);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 0u, 0u};
            return d;
        }

        /**
         * The ARM Commander. A builder that is mobile like any other
         * construction unit, and which the count is expected to include: a
         * commander with nothing to do is exactly what a player is looking for.
         */
        UnitDefinition makeCommanderDef()
        {
            auto d = makeConstructionBotDef();
            d.commander = true;
            d.maxHitPoints = 3000;
            return d;
        }

        UnitDefinition makeTankDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxVelocity = 5.6_ssf;
            d.acceleration = 0.0922_ssf;
            d.brakeRate = 0.55_ssf;
            d.turnRate = 185_ss;
            d.maxHitPoints = 300;
            d.sightDistance = 363u;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        UnitId addIdleBuilderUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos)
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");

            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = unitType;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at(unitType).maxHitPoints;
            unit.fireOrders = UnitFireOrders::FireAtWill;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        SimVector at(float x)
        {
            return SimVector(floatToSimScalar(x), 0_ss, 0_ss);
        }

        /** Everything the idle count is put together from, in one place. */
        struct Base
        {
            GameSimulation sim{GameSimulation(makeIdleBuilderTerrain(128, 128), 0u, 0, 0)};
            PlayerId us;
            PlayerId them;
            UnitId conOne;
            UnitId conTwo;
            UnitId factory;
            UnitId tank;
            UnitId commander;
            UnitId theirCon;
        };

        Base makeBase()
        {
            Base base;
            base.us = addIdleBuilderPlayer(base.sim, "us");
            base.them = addIdleBuilderPlayer(base.sim, "them");
            base.sim.unitDefinitions["armcon"] = makeConstructionBotDef();
            base.sim.unitDefinitions["armfak"] = makeFactoryDef();
            base.sim.unitDefinitions["armflash"] = makeTankDef();
            base.sim.unitDefinitions["armcom"] = makeCommanderDef();

            base.conOne = addIdleBuilderUnit(base.sim, "armcon", base.us, at(0.0f));
            base.conTwo = addIdleBuilderUnit(base.sim, "armcon", base.us, at(64.0f));
            base.factory = addIdleBuilderUnit(base.sim, "armfak", base.us, at(128.0f));
            base.tank = addIdleBuilderUnit(base.sim, "armflash", base.us, at(192.0f));
            base.commander = addIdleBuilderUnit(base.sim, "armcom", base.us, at(224.0f));
            base.theirCon = addIdleBuilderUnit(base.sim, "armcon", base.them, at(256.0f));
            return base;
        }

        /** The three of our own that the count should be reporting, in id order. */
        std::vector<UnitId> ownIdle(const Base& base)
        {
            return {base.conOne, base.conTwo, base.commander};
        }
    }

    // The count on the HUD is read out of the simulation once a frame, and the
    // rule that matters most about it is that it is only ever read: every field
    // it looks at is state the simulation already hashes, and the function hands
    // back a copy. These pin what it counts, because a count that quietly
    // starts counting the wrong units is a HUD that lies to the player about
    // their own base.
    TEST_CASE("the idle-builder count is a read of hashed state, not new state", "[idlebuilders]")
    {
        auto base = makeBase();
        auto& sim = base.sim;

        SECTION("a builder with an empty order queue counts")
        {
            REQUIRE(playerIdleBuilders(sim, base.us) == ownIdle(base));
        }

        SECTION("a commander counts, because it is a mobile builder that can be sent somewhere")
        {
            auto idle = playerIdleBuilders(sim, base.us);
            REQUIRE(std::find(idle.begin(), idle.end(), base.commander) != idle.end());
        }

        SECTION("a builder with work in hand does not")
        {
            sim.getUnitState(base.conOne).orders.push_back(MoveOrder(at(512.0f)));

            auto expected = ownIdle(base);
            expected.erase(expected.begin());
            REQUIRE(playerIdleBuilders(sim, base.us) == expected);
        }

        SECTION("a builder with a second job queued behind the first does not either")
        {
            // The queue is what the AI reads too, and a builder walking to a
            // site with something behind it is a builder with something to do.
            auto& unit = sim.getUnitState(base.conTwo);
            unit.orders.push_back(MoveOrder(at(512.0f)));
            unit.orders.push_back(MoveOrder(at(768.0f)));

            auto expected = ownIdle(base);
            expected.erase(expected.begin() + 1);
            REQUIRE(playerIdleBuilders(sim, base.us) == expected);
        }

        SECTION("a factory is not counted, however idle")
        {
            // A factory that is not building is not something a player can send
            // to a job, so counting it would promise a jump the click could not
            // make. It is the one thing here that is a builder, has an empty
            // queue, is alive and is the player's own.
            auto idle = playerIdleBuilders(sim, base.us);
            REQUIRE(std::find(idle.begin(), idle.end(), base.factory) == idle.end());
        }

        SECTION("a unit that is not a builder at all is not counted")
        {
            auto idle = playerIdleBuilders(sim, base.us);
            REQUIRE(std::find(idle.begin(), idle.end(), base.tank) == idle.end());
        }

        SECTION("another player's builders are not counted")
        {
            auto idle = playerIdleBuilders(sim, base.us);
            REQUIRE(std::find(idle.begin(), idle.end(), base.theirCon) == idle.end());
            REQUIRE(playerIdleBuilders(sim, base.them) == std::vector<UnitId>{base.theirCon});
        }

        SECTION("a builder a mission script holds is not counted")
        {
            sim.getUnitState(base.conOne).heldByMission = true;

            auto expected = ownIdle(base);
            expected.erase(expected.begin());
            REQUIRE(playerIdleBuilders(sim, base.us) == expected);
        }

        SECTION("a dead builder is not counted")
        {
            // isAlive() reads the unit's own life state, not its hit points:
            // a unit at zero is a wreck waiting to be cleaned up, and only
            // LifeState::Dead is gone.
            sim.getUnitState(base.conOne).lifeState = UnitState::LifeStateDead();

            auto expected = ownIdle(base);
            expected.erase(expected.begin());
            REQUIRE(playerIdleBuilders(sim, base.us) == expected);
        }

        SECTION("a unit whose type this simulation has no definition for is skipped, not thrown on")
        {
            // Asked once a frame, into the middle of drawing the HUD. A unit in
            // this state is one a save or a replay named a type this data set
            // has no definition for, and tryAddUnit will not let one be built --
            // so it is taken by taking the definition away from a unit that is
            // already standing there.
            sim.unitDefinitions.erase("armcon");

            auto expected = ownIdle(base);
            expected.erase(expected.begin());
            expected.erase(expected.begin());
            REQUIRE(playerIdleBuilders(sim, base.us) == expected);
        }

        SECTION("the order is by id, so the rotation does not jump about")
        {
            // Stated rather than inherited: the simulation's own walk happens to
            // come out in id order today because of how an id is laid out, and
            // the rotation should not rest on that.
            auto idle = playerIdleBuilders(sim, base.us);
            REQUIRE(std::is_sorted(idle.begin(), idle.end()));
        }

        SECTION("reading it changes nothing the next frame can see")
        {
            // The whole point of a read-only widget: asking the question must
            // leave the simulation's own answer exactly as it was. Compared
            // through the hash, which is what two peers have to agree on.
            auto before = sim.computeHash().value;
            playerIdleBuilders(sim, base.us);
            REQUIRE(sim.computeHash().value == before);
        }
    }

    TEST_CASE("the idle-builder sign walks a rotation", "[idlebuilders]")
    {
        const std::vector<UnitId> builders{UnitId(10), UnitId(11), UnitId(12)};

        SECTION("the first click goes to the first builder")
        {
            REQUIRE(nextIdleBuilderIndex(builders, std::nullopt) == 0);
        }

        SECTION("each click after that moves one along")
        {
            REQUIRE(nextIdleBuilderIndex(builders, UnitId(10)) == 1);
            REQUIRE(nextIdleBuilderIndex(builders, UnitId(11)) == 2);
        }

        SECTION("and it wraps round at the end")
        {
            REQUIRE(nextIdleBuilderIndex(builders, UnitId(12)) == 0);
        }

        SECTION("one builder is always the same builder")
        {
            const std::vector<UnitId> only{UnitId(10)};
            REQUIRE(nextIdleBuilderIndex(only, UnitId(10)) == 0);
        }

        SECTION("a builder that is no longer idle restarts the rotation")
        {
            // Rather than carrying on from the index it used to hold, which by
            // now names a different unit: the click would land on a builder the
            // player had never been shown.
            REQUIRE(nextIdleBuilderIndex(builders, UnitId(99)) == 0);
        }

        SECTION("with nothing idle there is nowhere to go")
        {
            REQUIRE(nextIdleBuilderIndex({}, UnitId(10)) == 0);
        }
    }
}
