#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/game/GameSimulationLoader.h>
#include <rwe/game/PlayerCommandApplication.h>
#include <rwe/game/save_util.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MissionScripts.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitPieceDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <string>

/**
 * A mission unit's InitialMission, run (issue #38, TOTALA-EXE-DATA.md
 * §114): the list the interpreter queued, stepped by MissionScripts.
 */
namespace rwe
{
    namespace
    {
        UnitDefinition definitionFrom(const std::string& unitName, const std::string& keys)
        {
            auto fbi = parseUnitFbi(parseTdfFromString("[UNITINFO]\n{\nUnitName=" + unitName + ";\nObjectname=model;\nSoundCategory=NOSOUND;\n" + keys + "\n}\n"));
            MovementClassDatabase movementClasses;
            return parseUnitDefinition(fbi, movementClasses);
        }

        /** Two players on a flat 1024-unit map: slot 0 the human, slot 1 the computer. */
        struct ScriptWorld
        {
            GameSimulation sim{makeFlatTerrain(64, 64), 0u, 0, 0};
            std::array<std::optional<PlayerId>, 10> slots;
            OtaSchema schema{};

            ScriptWorld()
            {
                std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
                sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
                auto script = makeEmptyCobScript({"base"});
                auto mobile = std::string("FootprintX=2;\nFootprintZ=2;\nBMcode=1;\nMaxDamage=200;\nBuildTime=100;\nMaxVelocity=2;\nAcceleration=0.5;\nBrakeRate=0.5;\nTurnRate=2000;\nSightDistance=300;\nCanMove=1;\nCanPatrol=1;\nCanGuard=1;");
                sim.unitDefinitions["KBOT"] = definitionFrom("KBOT", mobile + "\nCanAttack=1;");
                sim.unitDefinitions["TRUCK"] = definitionFrom("TRUCK", mobile);
                sim.unitDefinitions["HULL"] = definitionFrom("HULL", mobile + "\nTransportCapacity=5;\nTransportSize=3;");
                sim.unitDefinitions["GATE"] = definitionFrom("GATE", "FootprintX=2;\nFootprintZ=2;\nBMcode=0;\nMaxDamage=300;\nBuildTime=100;\nYardMap=oooo;");
                sim.unitDefinitions["PLANT"] = definitionFrom("PLANT", "FootprintX=2;\nFootprintZ=2;\nBMcode=0;\nMaxDamage=300;\nBuildTime=100;\nYardMap=oooo;\nBuilder=1;");
                for (const auto& [name, def] : sim.unitDefinitions)
                {
                    sim.unitScriptDefinitions[name] = *script;
                }
                slots[0] = addPlayer(sim, "human");
                slots[1] = addWellStockedPlayer(sim, "CORE");
                sim.getPlayer(*slots[1]).type = GamePlayerType::Computer;
            }

            /** A [units] record: Player 1 is the human, 2 the computer. */
            void add(const std::string& name, int player, int x, int z, const std::string& orders, const std::string& ident = "")
            {
                OtaMissionUnit u;
                u.unitName = name;
                u.player = player;
                u.xPos = x;
                u.zPos = z;
                u.ident = ident;
                u.initialMission = orders;
                u.orders = parseInitialMission(orders);
                schema.units.push_back(u);
            }

            std::vector<UnitId> spawn()
            {
                auto result = spawnMissionUnits(sim, schema, slots);
                REQUIRE(result.skipped.empty());
                return result.spawned;
            }

            UnitState& unit(UnitId id)
            {
                return sim.getUnitState(id);
            }

            /** The scripts' part of one tick, at the given game tick. */
            void runAt(unsigned int tick)
            {
                sim.gameTime = GameTime(tick);
                if (sim.missionScripts)
                {
                    sim.missionScripts->update(sim);
                }
            }

            bool running(UnitId id) const
            {
                return sim.missionScripts && sim.missionScripts->isRunning(id);
            }
        };

        template <typename T>
        const T* front(const UnitState& unit)
        {
            return unit.orders.empty() ? nullptr : std::get_if<T>(&unit.orders.front());
        }
    }

    TEST_CASE("a mission unit waits its seconds, moves, and is handed back", "[mission][script]")
    {
        ScriptWorld world;
        world.add("KBOT", 1, 100, 100, "w 2,m 600 100,");
        auto unit = world.spawn().at(0);
        REQUIRE(world.unit(unit).heldByMission);

        // Two seconds from when the wait first runs.
        world.runAt(1);
        REQUIRE(world.unit(unit).orders.empty());
        world.runAt(60);
        REQUIRE(world.unit(unit).orders.empty());
        world.runAt(61);
        auto move = front<MoveOrder>(world.unit(unit));
        REQUIRE(move != nullptr);
        REQUIRE(move->destination.x == world.sim.terrain.topLeftCoordinateToWorld(SimVector(600_ss, 0_ss, 100_ss)).x);
        REQUIRE(world.unit(unit).heldByMission);

        // The move done, the appended MAKESELECTABLE hands it back.
        world.unit(unit).orders.clear();
        world.runAt(62);
        REQUIRE_FALSE(world.unit(unit).heldByMission);
        REQUIRE_FALSE(world.running(unit));
    }

    TEST_CASE("a patrol or a point attack keeps its unit held for good", "[mission][script]")
    {
        ScriptWorld world;
        world.add("KBOT", 2, 100, 100, "p 500 500,");
        world.add("KBOT", 2, 200, 100, "a 500 500,");
        auto units = world.spawn();
        world.runAt(1);
        REQUIRE(front<PatrolOrder>(world.unit(units[0])) != nullptr);
        REQUIRE(front<AttackOrder>(world.unit(units[1])) != nullptr);
        // No MAKESELECTABLE was appended, so nothing ever hands them back.
        REQUIRE(world.sim.missionScripts->scripts.at(units[0].value).steps.size() == 1);
        REQUIRE(world.unit(units[0]).heldByMission);
        REQUIRE(world.unit(units[1]).heldByMission);
    }

    TEST_CASE("a wait with a radius ends when a seen enemy comes near", "[mission][script]")
    {
        ScriptWorld world;
        world.add("KBOT", 2, 500, 500, "w 1000 100,s,");
        auto unit = world.spawn().at(0);

        auto enemyAt = [&](float dx) {
            auto pos = world.unit(unit).position + SimVector(SimScalar(dx), 0_ss, 0_ss);
            return addUnitOfType(world.sim, "KBOT", *world.slots[0], pos, makeEmptyCobScript());
        };

        SECTION("far away, it waits on, looking every 150 to 179 ticks")
        {
            enemyAt(250.0f);
            world.sim.updateVisibility();
            world.runAt(1);
            REQUIRE(world.running(unit));
            auto wake = world.sim.missionScripts->scripts.at(unit.value).wakeAt;
            REQUIRE(wake.value >= 151);
            REQUIRE(wake.value <= 180);
            REQUIRE(world.sim.missionScripts->scripts.at(unit.value).steps.front().ticks < 30000);
        }

        SECTION("near, it is over at the first look")
        {
            enemyAt(90.0f);
            world.sim.updateVisibility();
            world.runAt(1);
            REQUIRE_FALSE(world.unit(unit).heldByMission);
        }

        SECTION("an Immune enemy does not count")
        {
            auto gate = enemyAt(90.0f);
            world.unit(gate).immune = true;
            world.sim.updateVisibility();
            world.runAt(1);
            REQUIRE(world.unit(unit).heldByMission);
        }
    }

    TEST_CASE("wa waits until the watched unit is hit, even before its turn", "[mission][script]")
    {
        ScriptWorld world;
        world.add("KBOT", 2, 100, 100, "w 5,wa,s,");
        world.add("TRUCK", 2, 300, 100, "wa BOSS,s,");
        world.add("GATE", 2, 500, 100, "", "boss");
        auto units = world.spawn();

        // Hit during the first unit's timed wait: the watch is already set.
        world.runAt(1);
        world.sim.applyDamage(units[0], 1u);
        world.runAt(200);
        REQUIRE_FALSE(world.unit(units[0]).heldByMission);

        // The second waits on the boss, not on itself.
        world.sim.applyDamage(units[1], 1u);
        world.runAt(201);
        REQUIRE(world.unit(units[1]).heldByMission);
        world.sim.applyDamage(units[2], 1u);
        world.runAt(202);
        REQUIRE_FALSE(world.unit(units[1]).heldByMission);
    }

    TEST_CASE("a hunter goes after every enemy of the type, then moves on", "[mission][script]")
    {
        ScriptWorld world;
        world.add("KBOT", 2, 100, 100, "a TRUCK,s,");
        world.add("TRUCK", 1, 400, 100, "");
        world.add("TRUCK", 1, 900, 900, "");
        // Its own side's trucks are not quarry.
        world.add("TRUCK", 2, 120, 100, "");
        auto units = world.spawn();
        auto hunter = units[0];
        // Immunity does not protect from a hunt (0x401E00 has no such test).
        world.unit(units[1]).immune = true;

        std::vector<UnitId> chased;
        for (unsigned int tick = 1; tick < 2000 && world.running(hunter); ++tick)
        {
            world.runAt(tick);
            if (auto attack = front<AttackOrder>(world.unit(hunter)))
            {
                auto target = std::get<UnitId>(attack->target);
                chased.push_back(target);
                // It dies, and the hunt goes on to the next.
                world.sim.killUnit(target);
                world.sim.units.remove(target);
                world.unit(hunter).orders.clear();
            }
        }
        REQUIRE(chased.size() == 2);
        REQUIRE(std::find(chased.begin(), chased.end(), units[1]) != chased.end());
        REQUIRE(std::find(chased.begin(), chased.end(), units[2]) != chased.end());
        REQUIRE(world.sim.tryGetUnitState(units[3]).has_value());
        REQUIRE_FALSE(world.unit(hunter).heldByMission);
    }

    TEST_CASE("the orders that act at once: standing orders, a transport, a guard, a bang", "[mission][script]")
    {
        ScriptWorld world;
        world.add("HULL", 2, 100, 100, "", "ship");
        world.add("KBOT", 2, 300, 100, "o 0 2,i SHIP,m 500 500,");
        world.add("TRUCK", 2, 500, 100, "g SHIP,");
        world.add("KBOT", 2, 700, 100, "w 1,d,");
        auto units = world.spawn();

        // Move order 0 is hold position, fire order 2 fire at will.
        REQUIRE(world.unit(units[1]).moveOrders == UnitMovementOrders::HoldPosition);
        REQUIRE(world.unit(units[1]).fireOrders == UnitFireOrders::FireAtWill);
        // Aboard the ship from the start, and its list waits to be set down.
        REQUIRE(world.unit(units[1]).carriedBy == std::optional<UnitId>(units[0]));
        world.runAt(1);
        REQUIRE(world.unit(units[1]).orders.empty());

        auto guard = front<GuardOrder>(world.unit(units[2]));
        REQUIRE(guard != nullptr);
        REQUIRE(guard->target == units[0]);

        world.runAt(31);
        REQUIRE(world.unit(units[3]).isDead());
    }

    TEST_CASE("a held unit takes no orders, and a handed-back one's order replaces its list", "[mission][script]")
    {
        ScriptWorld world;
        world.add("KBOT", 1, 100, 100, "w 1,");
        world.add("KBOT", 1, 300, 100, "s,w 100,m 500 500,");
        auto units = world.spawn();
        auto moveTo = PlayerUnitCommand::IssueOrder(MoveOrder(SimVector(0_ss, 0_ss, 0_ss)), PlayerUnitCommand::IssueOrder::IssueKind::Immediate);

        REQUIRE_FALSE(world.unit(units[0]).isSelectableBy(world.sim.unitDefinitions.at("KBOT"), *world.slots[0]));
        REQUIRE_FALSE(applyUnitCommandToSimulation(world.sim, *world.slots[0], PlayerUnitCommand(units[0], moveTo)));
        REQUIRE(world.unit(units[0]).orders.empty());

        // The second is handed back at once and then carries on with its
        // list, until the player gives it something else to do.
        world.runAt(1);
        REQUIRE_FALSE(world.unit(units[1]).heldByMission);
        REQUIRE(world.running(units[1]));
        REQUIRE(applyUnitCommandToSimulation(world.sim, *world.slots[0], PlayerUnitCommand(units[1], moveTo)));
        REQUIRE_FALSE(world.running(units[1]));
    }

    TEST_CASE("a computer's unit handed back gets the AI's standing orders and loses its Immunity", "[mission][script]")
    {
        ScriptWorld world;
        world.add("KBOT", 2, 100, 100, "o 0 0,s,");
        auto unit = world.spawn().at(0);
        world.unit(unit).immune = true;
        world.runAt(1);
        REQUIRE_FALSE(world.unit(unit).immune);
        REQUIRE(world.unit(unit).moveOrders == UnitMovementOrders::Roam);
        REQUIRE(world.unit(unit).fireOrders == UnitFireOrders::FireAtWill);
    }

    TEST_CASE("the scripts are saved, hashed and dumped", "[mission][script][save]")
    {
        ScriptWorld world;
        world.add("KBOT", 2, 100, 100, "w 10,wa,m 500 500,");
        auto unit = world.spawn().at(0);
        world.runAt(1);

        auto before = computeHashOf(world.sim);
        world.sim.missionScripts->unitDamaged(unit);
        REQUIRE(computeHashOf(world.sim) != before);

        auto saved = saveSimulationToJson(world.sim);
        REQUIRE(saved.contains("missionScripts"));
        GameSimulation loaded{makeFlatTerrain(64, 64), 0u, 0, 0};
        loaded.unitDefinitions = world.sim.unitDefinitions;
        loaded.unitScriptDefinitions = world.sim.unitScriptDefinitions;
        loaded.unitModelDefinitions = world.sim.unitModelDefinitions;
        loadSimulationFromJson(saved, loaded);
        REQUIRE(loaded.missionScripts);
        REQUIRE(*loaded.missionScripts == *world.sim.missionScripts);
        REQUIRE(computeHashOf(loaded) == computeHashOf(world.sim));
    }

    TEST_CASE("a patrol route goes round its points and back to where it set out", "[mission][script]")
    {
        // A run of `p` is one route, closed by a waypoint where the unit
        // stands when it sets out (0x43A020), and it goes round for ever.
        ScriptWorld world;
        world.add("KBOT", 2, 100, 100, "p 400 100,p 400 400,");
        auto unit = world.spawn().at(0);
        auto toWorld = [&](float x, float z) { return world.sim.terrain.topLeftCoordinateToWorld(SimVector(SimScalar(x), 0_ss, SimScalar(z))); };
        std::vector<SimVector> points{toWorld(400, 100), toWorld(400, 400), toWorld(100, 100)};
        std::vector<int> visits(points.size(), 0);
        std::optional<std::size_t> last;
        for (int i = 0; i < 6000; ++i)
        {
            world.sim.tick();
            const auto& position = world.unit(unit).position;
            for (std::size_t p = 0; p < points.size(); ++p)
            {
                auto dx = position.x - points[p].x;
                auto dz = position.z - points[p].z;
                if ((dx * dx) + (dz * dz) < 40_ss * 40_ss && last != p)
                {
                    ++visits[p];
                    last = p;
                }
            }
        }
        // Round at least twice, and it is still the mission's.
        for (auto v : visits)
        {
            REQUIRE(v >= 2);
        }
        REQUIRE(world.unit(unit).heldByMission);
    }

    TEST_CASE("a captured mission unit is the captor's, free of its list", "[mission][script]")
    {
        ScriptWorld world;
        world.add("KBOT", 2, 100, 100, "p 400 100,");
        world.add("KBOT", 1, 300, 300, "wa ENEMY,s,");
        world.schema.units[0].ident = "enemy";
        auto units = world.spawn();
        world.unit(units[0]).immune = true;
        world.runAt(1);

        world.sim.captureUnit(units[0], *world.slots[0], std::nullopt);
        REQUIRE_FALSE(world.unit(units[0]).heldByMission);
        REQUIRE_FALSE(world.unit(units[0]).immune);
        REQUIRE_FALSE(world.running(units[0]));
        REQUIRE(world.unit(units[0]).isSelectableBy(world.sim.unitDefinitions.at("KBOT"), *world.slots[0]));
        // The unit watching it saw it go, as it would have seen it die.
        world.runAt(2);
        REQUIRE_FALSE(world.unit(units[1]).heldByMission);
    }

    TEST_CASE("Stop ends what is left of a handed-back unit's list", "[mission][script]")
    {
        ScriptWorld world;
        world.add("KBOT", 1, 100, 100, "s,w 3,m 100 600,");
        auto unit = world.spawn().at(0);
        world.runAt(1);
        REQUIRE_FALSE(world.unit(unit).heldByMission);
        REQUIRE(applyUnitCommandToSimulation(world.sim, *world.slots[0], PlayerUnitCommand(unit, PlayerUnitCommand::Stop())));
        REQUIRE_FALSE(world.running(unit));
        world.runAt(200);
        REQUIRE(world.unit(unit).orders.empty());
    }

    TEST_CASE("a computer's unit counts its wait aboard its transport; a human's does not", "[mission][script]")
    {
        ScriptWorld world;
        world.add("HULL", 2, 100, 100, "", "ship");
        world.add("KBOT", 2, 300, 100, "i SHIP,w 2,s,");
        world.add("HULL", 1, 500, 500, "", "boat");
        world.add("KBOT", 1, 700, 500, "i BOAT,w 2,s,");
        auto units = world.spawn();
        REQUIRE(world.unit(units[1]).carriedBy);
        REQUIRE(world.unit(units[3]).carriedBy);
        for (unsigned int tick = 1; tick <= 70; ++tick)
        {
            world.runAt(tick);
        }
        REQUIRE_FALSE(world.unit(units[1]).heldByMission);
        REQUIRE(world.unit(units[3]).heldByMission);
    }

    TEST_CASE("a plant builds its count from the queue, and a builder builds where it is told", "[mission][script]")
    {
        ScriptWorld world;
        world.add("PLANT", 2, 100, 100, "b KBOT 2,");
        world.add("TRUCK", 2, 400, 100, "b GATE 1 600 600,");
        auto units = world.spawn();
        world.runAt(1);
        REQUIRE(world.unit(units[0]).buildQueue.size() == 1);
        REQUIRE(world.unit(units[0]).buildQueue.front() == std::make_pair(std::string("KBOT"), 2));
        auto build = front<BuildOrder>(world.unit(units[1]));
        REQUIRE(build != nullptr);
        REQUIRE(build->unitType == "GATE");

        // The plant's list moves on once its queue has been built.
        REQUIRE(world.unit(units[0]).heldByMission);
        world.unit(units[0]).buildQueue.clear();
        world.runAt(2);
        REQUIRE_FALSE(world.unit(units[0]).heldByMission);
    }

    TEST_CASE("the small cases: a name that did not scan, a move for a building, a reclaim, a lone o", "[mission][script]")
    {
        ScriptWorld world;
        world.add("TRUCK", 2, 100, 100, "");
        world.add("KBOT", 2, 300, 100, "g,");
        world.add("GATE", 2, 500, 100, "m 100 100,w 1,");
        world.add("KBOT", 1, 700, 100, "wa,s,");
        world.add("KBOT", 2, 900, 100, "o 0 0,");
        auto units = world.spawn();

        // `g` with no name guards nothing and holds nothing.
        REQUIRE_FALSE(world.unit(units[1]).heldByMission);
        REQUIRE_FALSE(world.running(units[1]));

        // A building's move is passed over and its wait starts.
        world.runAt(1);
        REQUIRE(world.unit(units[2]).orders.empty());
        REQUIRE(world.sim.missionScripts->scripts.at(units[2].value).steps.front().kind == MissionStep::Kind::Wait);

        // Being reclaimed is being hit.
        world.sim.reclaimUnitStep(units[3], *world.slots[1], 1u);
        world.runAt(2);
        REQUIRE_FALSE(world.unit(units[3]).heldByMission);

        // A computer's unit its list does not hold is the AI's at once, and
        // gets the AI's standing orders over the `o`.
        REQUIRE(world.unit(units[4]).moveOrders == UnitMovementOrders::Roam);
        REQUIRE(world.unit(units[4]).fireOrders == UnitFireOrders::FireAtWill);
    }
}
