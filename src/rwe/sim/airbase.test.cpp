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

        /** ARMFIG, the ARM Freedom Fighter, with its own shipped numbers. */
        UnitDefinition makeAirBaseFighterDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canFly = true;
            d.cruiseAltitude = 110_ss;
            d.maxVelocity = 10_ss;
            d.acceleration = 0.35_ssf;
            d.brakeRate = 6.0_ssf;
            d.turnRate = 512_ss;
            d.maneuverLeashLength = 1280_ss;
            d.sightDistance = 350u;
            d.maxHitPoints = 196;
            d.buildTime = 9182u;
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

            // Finished, not a nanoframe. These definitions carry the real
            // shipped BuildTime -- ARMFIG's is 9182 -- and tryAddUnit does not
            // wind the counter the way a completed build would, so without
            // this every unit here is permanently under construction: the
            // repair search skips it and the behaviour update never runs it.
            unit.buildTimeCompleted = sim.unitDefinitions.at(unitType).buildTime;
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

TEST_CASE("a pad someone is already on their way to is taken", "[airbase]")
    {
        // "When a plane is making its way back to the repair pad, that pad is
        // considered to be occupied (even if the unit isn't there yet), so
        // other damaged aircraft will not use that pad until the occupying
        // aircraft has been repaired and has left."
        //
        // The same rule the original prints "Landing aborted: no pads
        // available" for (0x501C30, from the re-test at 0x411DEB).
        AirBaseFixture f;
        auto pad = f.addPad(f.us, SimVector(500_ss, 0_ss, 0_ss));
        f.damageFighterTo(100);

        auto second = spawnAirBaseUnit(f.sim, "fighter", f.us, SimVector(40_ss, 200_ss, 0_ss), f.script);
        {
            auto& s = f.sim.getUnitState(second);
            s.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            s.hitPoints = 100;
            f.sim.flyingUnitsSet.insert(second);
        }

        // The first aircraft claims it merely by holding the order.
        f.sim.getUnitState(f.fighter).orders.push_front(LandOnAirBaseOrder(pad));

        const auto& secondState = f.sim.getUnitState(second);
        ConstUnitInfo secondInfo(second, &secondState, &f.sim.unitDefinitions.at(secondState.unitType));
        REQUIRE_FALSE(findAirBaseToLandOn(f.sim, secondInfo).has_value());

        // With a second pad there is somewhere else to go.
        auto spare = f.addPad(f.us, SimVector(600_ss, 0_ss, 0_ss));
        auto chosen = findAirBaseToLandOn(f.sim, secondInfo);
        REQUIRE(chosen.has_value());
        REQUIRE(*chosen == spare);
    }

    TEST_CASE("a pad is still taken by an aircraft that has finished repairing but not left", "[airbase]")
    {
        // "...until the occupying aircraft has been repaired AND HAS LEFT."
        // Sitting on the pad at full health still holds it.
        AirBaseFixture f;
        auto padPosition = SimVector(500_ss, 0_ss, 0_ss);
        auto pad = f.addPad(f.us, padPosition);

        auto resident = spawnAirBaseUnit(f.sim, "fighter", f.us, padPosition, f.script);
        f.sim.getUnitState(resident).physics = UnitPhysicsInfoGround();

        f.damageFighterTo(100);
        REQUIRE_FALSE(findAirBaseToLandOn(f.sim, f.fighterInfo()).has_value());

        // Once it takes off again the pad is free.
        {
            auto& r = f.sim.getUnitState(resident);
            r.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            r.position = SimVector(1200_ss, 200_ss, 1200_ss);
            f.sim.flyingUnitsSet.insert(resident);
        }
        auto chosen = findAirBaseToLandOn(f.sim, f.fighterInfo());
        REQUIRE(chosen.has_value());
        REQUIRE(*chosen == pad);
    }

    TEST_CASE("switching a pad off turns away new arrivals but not one already coming", "[airbase]")
    {
        // "turning an aircraft repair pad 'Off' would stop aircraft from
        // returning to it. However, those that were already making their way
        // towards it will continue towards it."
        //
        // The split is in the binary: the query walks the owner's air base
        // list, which holds only switched-on pads (0x40ABCF), while the
        // re-test on the way in (0x411DEB -> 0x47E570) asks whether the pad
        // is free and never asks about the switch.
        AirBaseFixture f;
        auto pad = f.addPad(f.us, SimVector(500_ss, 0_ss, 0_ss));
        f.damageFighterTo(100);
        f.sim.getUnitState(f.fighter).orders.push_front(LandOnAirBaseOrder(pad));

        // Switch it off underneath the aircraft already en route.
        f.sim.getUnitState(pad).activated = false;

        for (int tick = 0; tick < 30; ++tick)
        {
            f.sim.tick();
        }

        // It is still going: the order was not dropped.
        const auto& orders = f.sim.getUnitState(f.fighter).orders;
        REQUIRE_FALSE(orders.empty());
        REQUIRE(std::holds_alternative<LandOnAirBaseOrder>(orders.front()));

        // But nothing new is sent there.
        auto second = spawnAirBaseUnit(f.sim, "fighter", f.us, SimVector(40_ss, 200_ss, 0_ss), f.script);
        {
            auto& s = f.sim.getUnitState(second);
            s.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            s.hitPoints = 100;
            f.sim.flyingUnitsSet.insert(second);
        }
        const auto& secondState = f.sim.getUnitState(second);
        ConstUnitInfo secondInfo(second, &secondState, &f.sim.unitDefinitions.at(secondState.unitType));
        REQUIRE_FALSE(findAirBaseToLandOn(f.sim, secondInfo).has_value());
    }

    TEST_CASE("a healed aircraft goes back to what it was doing", "[airbase]")
    {
        // "Once they have healed themself, they will return to what they were
        // previously doing." The landing order deletes itself when the hit
        // points meet the maximum, and the mission underneath is untouched,
        // so the patrol is simply next in the queue again.
        AirBaseFixture f;
        auto padPosition = SimVector(500_ss, 0_ss, 0_ss);
        auto pad = f.addPad(f.us, padPosition);

        {
            auto& fighter = f.sim.getUnitState(f.fighter);
            fighter.orders.push_back(PatrolOrder(SimVector(-800_ss, 0_ss, 0_ss)));
            fighter.orders.push_front(LandOnAirBaseOrder(pad));
            fighter.hitPoints = 100;
            fighter.position = padPosition;
            fighter.physics = UnitPhysicsInfoGround();
            f.sim.flyingUnitsSet.erase(f.fighter);
        }

        // While it is still hurt the landing order stands.
        f.sim.tick();
        REQUIRE(std::holds_alternative<LandOnAirBaseOrder>(f.sim.getUnitState(f.fighter).orders.front()));

        // Mended, and the patrol underneath comes back.
        f.sim.getUnitState(f.fighter).hitPoints = f.sim.unitDefinitions.at("fighter").maxHitPoints;
        for (int tick = 0; tick < 5; ++tick)
        {
            f.sim.tick();
        }

        const auto& orders = f.sim.getUnitState(f.fighter).orders;
        REQUIRE_FALSE(orders.empty());
        REQUIRE(std::holds_alternative<PatrolOrder>(orders.front()));
    }

    TEST_CASE("a one point patrol is enough to send a grounded damaged aircraft for repair", "[airbase]")
    {
        // "If you have some damaged aircraft that you have manually returned
        // from a fight, and they are sitting on the ground and you want them
        // to repair themselves, set up a 1 point Patrol route (where they
        // are), and those planes that are heavily damaged will go off and get
        // repaired."
        //
        // It follows from patrol being one of the missions that carries the
        // health test, and it is worth pinning because it is the practical
        // way a player uses all of this.
        AirBaseFixture f;
        auto pad = f.addPad(f.us, SimVector(500_ss, 0_ss, 0_ss));
        f.damageFighterTo(100);

        {
            auto& fighter = f.sim.getUnitState(f.fighter);
            fighter.orders.push_back(PatrolOrder(fighter.position));
        }

        std::optional<UnitId> target;
        for (int tick = 0; tick < 60 && !target; ++tick)
        {
            f.sim.tick();
            const auto& orders = f.sim.getUnitState(f.fighter).orders;
            if (!orders.empty())
            {
                if (auto landing = std::get_if<LandOnAirBaseOrder>(&orders.front()))
                {
                    target = landing->target;
                }
            }
        }

        REQUIRE(target.has_value());
        REQUIRE(*target == pad);
    }

    TEST_CASE("an aircraft only looks for a repair pad below three quarters health", "[airbase]")
    {
        // 0x410518: (maxHitPoints >> 2) * 3, and the jump out is jae, so the
        // aircraft must be strictly under it. ARMFIG's 196 makes it 147.
        AirBaseFixture f;
        f.addPad(f.us, SimVector(500_ss, 0_ss, 0_ss));

        f.damageFighterTo(196);
        REQUIRE_FALSE(findAirBaseToLandOn(f.sim, f.fighterInfo()).has_value());

        f.damageFighterTo(147);
        REQUIRE_FALSE(findAirBaseToLandOn(f.sim, f.fighterInfo()).has_value());

        f.damageFighterTo(146);
        REQUIRE(findAirBaseToLandOn(f.sim, f.fighterInfo()).has_value());
    }

    TEST_CASE("the threshold truncates the quarter, not the product", "[airbase]")
    {
        // 0x41052B-0x41052E is shr eax,2 then lea eax,[eax+eax*2]: the divide
        // by four happens first and throws away the remainder. ARMHAWK is the
        // shipped aircraft that shows it -- 510 hit points give a threshold of
        // (510 >> 2) * 3 = 381, where multiplying first would give 382. With
        // the rule being `hitPoints < threshold`, a Hawk on exactly 381 stays
        // out under this reading and would go to the pad under the other, so
        // 381 is the value that tells the two apart.
        UnitDefinition d{};
        d.maxHitPoints = 510;
        std::vector<UnitMesh> noPieces;
        UnitState state(noPieces, std::unique_ptr<CobEnvironment>());

        state.hitPoints = 382;
        REQUIRE_FALSE(aircraftWantsRepair(state, d));

        // The discriminating case: multiplying first would send this one.
        state.hitPoints = 381;
        REQUIRE_FALSE(aircraftWantsRepair(state, d));

        state.hitPoints = 380;
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

    TEST_CASE("a damaged aircraft with nothing to do lands where it stands", "[airbase]")
    {
        // An aircraft only breaks off for a pad from a mission that tests for
        // one, and the idle mission is not among them. VTOL_SeekAttack is:
        // its handler at 0x4103E0 -- the mission record at 0x4FD3DA names it,
        // status text "Seeking to attack" -- runs the three-quarters health
        // test at 0x410518 and swaps in a VTOL_LANDING carrying the pad at
        // 0x4105B9. A plane merely standing about never reaches that code, so
        // it sets down under itself however badly hurt it is.
        AirBaseFixture f;
        f.addPad(f.us, SimVector(2000_ss, 0_ss, 0_ss));
        f.damageFighterTo(100);

        auto start = f.sim.getUnitState(f.fighter).position;
        for (int tick = 0; tick < 60; ++tick)
        {
            f.sim.tick();
        }

        auto moved = f.sim.getUnitState(f.fighter).position.x - start.x;
        INFO("drifted " << simScalarToFloat(moved) << " world units on x");
        REQUIRE(moved < 100_ss);
        REQUIRE(moved > -100_ss);

        // And it never took an order to go anywhere.
        const auto& orders = f.sim.getUnitState(f.fighter).orders;
        REQUIRE((orders.empty() || !std::holds_alternative<LandOnAirBaseOrder>(orders.front())));
    }

    TEST_CASE("a damaged aircraft on a mission breaks off for the pad", "[airbase]")
    {
        // The other half of the same rule, and the one that matters: patrol,
        // guard and the attack missions all carry the health test, so a plane
        // with something to do abandons it and goes to be mended. The patrol
        // order behind it is left in place, so the route resumes afterwards.
        AirBaseFixture f;
        auto pad = f.addPad(f.us, SimVector(500_ss, 0_ss, 0_ss));
        f.damageFighterTo(100);

        {
            auto& fighter = f.sim.getUnitState(f.fighter);
            fighter.orders.push_back(PatrolOrder(SimVector(-800_ss, 0_ss, 0_ss)));
        }

        std::optional<UnitId> landingTarget;
        for (int tick = 0; tick < 60 && !landingTarget; ++tick)
        {
            f.sim.tick();
            const auto& orders = f.sim.getUnitState(f.fighter).orders;
            if (!orders.empty())
            {
                if (auto landing = std::get_if<LandOnAirBaseOrder>(&orders.front()))
                {
                    landingTarget = landing->target;
                }
            }
        }

        REQUIRE(landingTarget.has_value());
        REQUIRE(*landingTarget == pad);

        // The patrol is still underneath it, waiting.
        const auto& orders = f.sim.getUnitState(f.fighter).orders;
        REQUIRE(orders.size() >= 2);
        REQUIRE(std::holds_alternative<PatrolOrder>(orders.back()));
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
