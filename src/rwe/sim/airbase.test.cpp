#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/LosTables.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <random>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeAirBaseTerrain(int width, int height)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addAirBasePlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("player"),
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

        std::shared_ptr<CobScript> makeAirBaseScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        void registerAirBaseModel(GameSimulation& sim, const std::string& objectName)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions[objectName] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /** The ARM Freedom Fighter: a plain fighter with nothing special about it. */
        UnitDefinition makeAirBaseFighterDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canFly = true;
            d.cruiseAltitude = 200_ss;
            d.maxVelocity = 10_ss;
            d.acceleration = 0.1_ssf;
            d.brakeRate = 0.5_ssf;
            d.turnRate = 500_ss;
            d.maneuverLeashLength = 1280_ss;
            d.sightDistance = 350u;
            d.maxHitPoints = 400;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        /** ARMASP, the Air Repair Pad: Builder=1, WorkerTime=200, IsAirBase=1. */
        UnitDefinition makeAirBasePadDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = false;
            d.canMove = false;
            d.builder = true;
            d.isAirBase = true;
            d.onOffable = true;
            d.activateWhenBuilt = true;
            d.workerTimePerTick = 6u;
            d.sightDistance = 175u;
            d.maxHitPoints = 680;
            d.buildTime = 0u;
            // ARMASP's yardmap is sixteen open cells, so nothing is blocked
            // from standing on the pad — which is what lets an aircraft come
            // down on top of it.
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 0u, 0u};
            d.yardMap = Grid<YardMapCell>(4, 4, YardMapCell::GroundPassable);
            return d;
        }

        UnitId spawnAirBaseUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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
            unit.hitPoints = sim.unitDefinitions.at(unitType).maxHitPoints;
            unit.fireOrders = UnitFireOrders::FireAtWill;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        struct AirBaseFixture
        {
            std::shared_ptr<CobScript> script;
            GameSimulation sim;
            PlayerId us;
            PlayerId them;
            UnitId fighter;

            AirBaseFixture()
                : script(makeAirBaseScript()),
                  sim(makeAirBaseTerrain(512, 512), 0u, 0, 0),
                  us(addAirBasePlayer(sim)),
                  them(addAirBasePlayer(sim)),
                  fighter(UnitId(0))
            {
                sim.unitDefinitions["fighter"] = makeAirBaseFighterDef();
                sim.unitDefinitions["pad"] = makeAirBasePadDef();
                registerAirBaseModel(sim, "model");
                sim.losTables = generateLosTables(8);

                fighter = spawnAirBaseUnit(sim, "fighter", us, SimVector(0_ss, 200_ss, 0_ss), script);
                auto& f = sim.getUnitState(fighter);
                f.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
                sim.flyingUnitsSet.insert(fighter);
            }

            UnitId addPad(PlayerId owner, const SimVector& position)
            {
                auto id = spawnAirBaseUnit(sim, "pad", owner, position, script);
                sim.getUnitState(id).activated = true;
                return id;
            }

            ConstUnitInfo fighterInfo()
            {
                const auto& state = sim.getUnitState(fighter);
                return ConstUnitInfo(fighter, &state, &sim.unitDefinitions.at(state.unitType));
            }

            void damageFighterTo(unsigned int hitPoints)
            {
                sim.getUnitState(fighter).hitPoints = hitPoints;
            }
        };
    }

    TEST_CASE("an aircraft only looks for a repair pad below three quarters health", "[airbase]")
    {
        // 0x410518: (maxHitPoints >> 2) * 3, and the jump out is jae, so the
        // aircraft must be strictly under it. 400 max makes the threshold 300.
        AirBaseFixture f;
        f.addPad(f.us, SimVector(500_ss, 0_ss, 0_ss));

        f.damageFighterTo(400);
        REQUIRE_FALSE(findAirBaseToLandOn(f.sim, f.fighterInfo()).has_value());

        f.damageFighterTo(300);
        REQUIRE_FALSE(findAirBaseToLandOn(f.sim, f.fighterInfo()).has_value());

        f.damageFighterTo(299);
        REQUIRE(findAirBaseToLandOn(f.sim, f.fighterInfo()).has_value());
    }

    TEST_CASE("the threshold truncates the quarter, not the product", "[airbase]")
    {
        // 0x41052B-0x41052E is shr eax,2 then lea eax,[eax+eax*2]: the divide
        // by four happens first and throws away the remainder, so a 402 hit
        // point aircraft gets a threshold of 300 and not the 301 that
        // multiplying first would give. Anything sitting between the two
        // readings goes to the pad under one and not under the other.
        UnitDefinition d{};
        d.maxHitPoints = 402;
        std::vector<UnitMesh> noPieces;
        UnitState state(noPieces, std::unique_ptr<CobEnvironment>());

        state.hitPoints = 300;
        REQUIRE_FALSE(aircraftWantsRepair(state, d));

        state.hitPoints = 299;
        REQUIRE(aircraftWantsRepair(state, d));
    }

    TEST_CASE("a repair pad is only picked up inside the search radius", "[airbase]")
    {
        // The seven callers all pass 0xf00 and the query squares it, so a pad
        // exactly 3840 out is still in and one unit further is not.
        AirBaseFixture f;
        f.damageFighterTo(100);

        auto near = f.addPad(f.us, SimVector(3840_ss, 0_ss, 0_ss));
        auto found = findAirBaseToLandOn(f.sim, f.fighterInfo());
        REQUIRE(found.has_value());
        REQUIRE(*found == near);

        f.sim.getUnitState(near).position = SimVector(3841_ss, 0_ss, 0_ss);
        REQUIRE_FALSE(findAirBaseToLandOn(f.sim, f.fighterInfo()).has_value());
    }

    TEST_CASE("the search radius is flat: height above the pad does not count against it", "[airbase]")
    {
        // 0x40B5A0 reads only the x and z words of the position and never
        // touches y, so an aircraft at cruise altitude is not pushed out of
        // range by its own height.
        AirBaseFixture f;
        f.damageFighterTo(100);
        f.sim.getUnitState(f.fighter).position = SimVector(0_ss, 2000_ss, 0_ss);
        f.addPad(f.us, SimVector(3800_ss, 0_ss, 0_ss));

        REQUIRE(findAirBaseToLandOn(f.sim, f.fighterInfo()).has_value());
    }

    TEST_CASE("a pad that is switched off is not a pad", "[airbase]")
    {
        // The list only holds units with the on/off bit at unit+0x10E bit 0
        // set (0x40ABCF), which ACTIVATE sets and DEACTIVATE clears.
        AirBaseFixture f;
        f.damageFighterTo(100);
        auto pad = f.addPad(f.us, SimVector(500_ss, 0_ss, 0_ss));

        REQUIRE(findAirBaseToLandOn(f.sim, f.fighterInfo()).has_value());

        f.sim.getUnitState(pad).activated = false;
        REQUIRE_FALSE(findAirBaseToLandOn(f.sim, f.fighterInfo()).has_value());
    }

    TEST_CASE("an IsAirBase unit that is not a builder is not a pad", "[airbase]")
    {
        // 0x40ABC6 tests Builder first and IsAirBase second; both of the
        // shipped pads and both carriers set Builder and a WorkerTime.
        AirBaseFixture f;
        f.damageFighterTo(100);
        f.addPad(f.us, SimVector(500_ss, 0_ss, 0_ss));

        f.sim.unitDefinitions.at("pad").builder = false;
        REQUIRE_FALSE(findAirBaseToLandOn(f.sim, f.fighterInfo()).has_value());
    }

    TEST_CASE("an enemy repair pad is not somewhere to land", "[airbase]")
    {
        // The query is handed a player index and walks that player's own list
        // (0x40B537).
        AirBaseFixture f;
        f.damageFighterTo(100);
        f.addPad(f.them, SimVector(500_ss, 0_ss, 0_ss));

        REQUIRE_FALSE(findAirBaseToLandOn(f.sim, f.fighterInfo()).has_value());
    }

    TEST_CASE("with two pads in range the choice is a draw, and with one it is not", "[airbase]")
    {
        // 0x4B6C30 returns zero without advancing the generator when it is
        // asked for a number below two, and otherwise steps the generator
        // once and takes the remainder. Every peer has to draw the same
        // number of times or the simulations part company, so the early
        // return is as much a determinism rule as a faithfulness one.
        AirBaseFixture f;
        f.damageFighterTo(100);
        auto first = f.addPad(f.us, SimVector(500_ss, 0_ss, 0_ss));

        auto rngBefore = f.sim.rng;
        REQUIRE(*findAirBaseToLandOn(f.sim, f.fighterInfo()) == first);
        REQUIRE(f.sim.rng == rngBefore);

        auto second = f.addPad(f.us, SimVector(-500_ss, 0_ss, 0_ss));
        std::minstd_rand expected = f.sim.rng;
        auto expectedIndex = expected() % 2u;

        auto chosen = *findAirBaseToLandOn(f.sim, f.fighterInfo());
        REQUIRE(f.sim.rng == expected);
        REQUIRE(chosen == (expectedIndex == 0 ? first : second));
    }

    TEST_CASE("a damaged aircraft with nothing to do flies to the pad instead of setting down", "[airbase]")
    {
        // The whole point of the flag: the original swaps the standby mission
        // for a VTOL_LANDING carrying the pad (0x4105B9), so the aircraft
        // crosses the map to the pad rather than landing on the spot.
        AirBaseFixture f;
        auto pad = f.addPad(f.us, SimVector(2000_ss, 0_ss, 0_ss));
        f.damageFighterTo(100);

        auto start = f.sim.getUnitState(f.fighter).position;
        for (int tick = 0; tick < 60; ++tick)
        {
            f.sim.tick();
        }

        auto padPosition = f.sim.getUnitState(pad).position;
        auto flatDistance = [](const SimVector& a, const SimVector& b) {
            auto dx = a.x - b.x;
            auto dz = a.z - b.z;
            return rweSqrt((dx * dx) + (dz * dz));
        };
        auto closed = flatDistance(start, padPosition) - flatDistance(f.sim.getUnitState(f.fighter).position, padPosition);
        INFO("closed " << simScalarToFloat(closed) << " world units on the pad in two seconds");
        REQUIRE(closed > 100_ss);
    }

    TEST_CASE("an undamaged aircraft with nothing to do still lands where it stands", "[airbase]")
    {
        // Above three quarters health the pad is never asked about, so the
        // ordinary landing search is what runs and the aircraft sets down
        // underneath itself.
        AirBaseFixture f;
        f.addPad(f.us, SimVector(2000_ss, 0_ss, 0_ss));

        auto start = f.sim.getUnitState(f.fighter).position;
        for (int tick = 0; tick < 60; ++tick)
        {
            f.sim.tick();
        }

        auto moved = f.sim.getUnitState(f.fighter).position.x - start.x;
        INFO("drifted " << simScalarToFloat(moved) << " world units on x");
        REQUIRE(moved < 100_ss);
        REQUIRE(moved > -100_ss);
    }

    TEST_CASE("a repair pad mends the aircraft parked on it", "[airbase]")
    {
        // Flying to a pad was only ever half the behaviour: the pad is a
        // builder and mends what sits on it. Without this an aircraft flew to
        // a pad, landed, and stayed damaged for ever.
        AirBaseFixture f;
        auto padPosition = SimVector(500_ss, 0_ss, 0_ss);
        auto padId = f.addPad(f.us, padPosition);

        SECTION("a landed, damaged aircraft on the pad is its patient")
        {
            f.damageFighterTo(100);
            auto& fighterState = f.sim.getUnitState(f.fighter);
            fighterState.position = padPosition;
            fighterState.physics = UnitPhysicsInfoGround();
            f.sim.flyingUnitsSet.erase(f.fighter);

            const auto& padState = f.sim.getUnitState(padId);
            ConstUnitInfo padInfo(padId, &padState, &f.sim.unitDefinitions.at(padState.unitType));
            auto patient = findAircraftToRepairOnPad(f.sim, padInfo);
            REQUIRE(patient.has_value());
            REQUIRE(*patient == f.fighter);
        }

        SECTION("an aircraft still in the air is not worked on")
        {
            f.damageFighterTo(100);
            f.sim.getUnitState(f.fighter).position = padPosition;

            const auto& padState = f.sim.getUnitState(padId);
            ConstUnitInfo padInfo(padId, &padState, &f.sim.unitDefinitions.at(padState.unitType));
            REQUIRE_FALSE(findAircraftToRepairOnPad(f.sim, padInfo).has_value());
        }

        SECTION("an undamaged aircraft is left alone")
        {
            auto& fighterState = f.sim.getUnitState(f.fighter);
            fighterState.position = padPosition;
            fighterState.physics = UnitPhysicsInfoGround();
            f.sim.flyingUnitsSet.erase(f.fighter);

            const auto& padState = f.sim.getUnitState(padId);
            ConstUnitInfo padInfo(padId, &padState, &f.sim.unitDefinitions.at(padState.unitType));
            REQUIRE_FALSE(findAircraftToRepairOnPad(f.sim, padInfo).has_value());
        }
    }
}
