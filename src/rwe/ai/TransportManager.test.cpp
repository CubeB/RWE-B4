#include <algorithm>
#include <filesystem>
#include <fstream>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/MapIntel.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>

/**
 * Sea transports for the skirmish AI (fork issue #26): naval reachability
 * wired up in AiPlayerController, TransportManager booking a hull-borne
 * ferry across water instead of only ever flying one, and the passenger
 * filter refusing a unit the simulation would refuse to load anyway.
 *
 * Modelled on the air-ferry fixture in AiBehaviour.test.cpp, but kept in its
 * own file with its own copies of the small local helpers -- there is no
 * header to share them from without moving them, and sim_test_util.h is for
 * fixtures more than one file already needed.
 */
namespace rwe
{
    namespace
    {
        PlayerId addPlayer(GameSimulation& sim, const std::string& name, GamePlayerType type, const std::string& side)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                type,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                side,
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
            return sim.addPlayer(p);
        }

        UnitDefinition makeDef(bool commander, bool builder, bool mobile, const std::string& weapon, unsigned int sight)
        {
            UnitDefinition d{};
            d.commander = commander;
            d.builder = builder;
            d.isMobile = mobile;
            d.canMove = mobile;
            d.canAttack = !weapon.empty();
            d.weapon1 = weapon;
            d.maxHitPoints = 100;
            d.buildTime = 100u;
            d.buildCostMetal = Metal(100.0f);
            d.buildCostEnergy = Energy(100.0f);
            d.sightDistance = sight;
            d.maxVelocity = 2_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            // Ordinary kbot movement: a 2x2 footprint and no wading -- any
            // depth at all stops it, which is what makes the channel below a
            // real barrier rather than a shallow puddle.
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
            return d;
        }

        /**
         * Two banks of land either side of a channel too deep for a kbot,
         * running north to south -- the same idea as ai_test_util.h's
         * makeChannelTerrain, but wider: 24 heightmap tiles rather than 8,
         * world x roughly -192..192. A hull's own footprint is labelled by
         * its top-left corner, not its centre, so a channel only a couple of
         * tiles wider than the ship's 6x6 footprint leaves almost no column
         * that is a valid top-left for it; this width gives room for the
         * naval queries below to land on solid water without hand-picking
         * exact tile-aligned coordinates. It is not folded into the shared
         * fixture because the two widths are both load-bearing: the AI tests'
         * geometry (bank positions, unload destinations) is written against
         * world x -64..64, and this test's against x -192..192.
         */
        MapTerrain makeWideChannelTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(60));
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 20; x < 44; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(0));
                }
            }
            return MapTerrain(std::move(heights), 30_ss);
        }

        /** All dry: the control for makeWideChannelTerrain, with no navigable water at all. */
        MapTerrain makeDryTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(60));
            return MapTerrain(std::move(heights), 30_ss);
        }

        void defineLandUnits(GameSimulation& sim)
        {
            WeaponDefinition laser{};
            laser.maxRange = 200_ss;
            laser.reloadTime = 1_ss;
            laser.burst = 1;
            laser.damage["DEFAULT"] = 30u;
            sim.weaponDefinitions["LASER"] = laser;

            sim.unitDefinitions["ARMCOM"] = makeDef(true, true, true, "", 300u);
            sim.unitDefinitions["ARMLAB"] = makeDef(false, true, false, "", 100u);
            sim.unitDefinitions["ARMCK"] = makeDef(false, true, true, "", 100u);
            sim.unitDefinitions["ARMPW"] = makeDef(false, false, true, "LASER", 200u);
            // The enemy: an unarmed building, so it neither trips Defend
            // (enemiesNearBase requires isArmed) nor needs a weapon of its
            // own to be worth attacking once its position is known.
            sim.unitDefinitions["ARMSOLAR"] = makeDef(false, false, false, "", 50u);

            // A unit that needs water under it, for the passenger filter.
            // Real shipped data (see AiSideUnits.h, already read out of the
            // FBI files): ARMSUB is 3x3, which clears the ship's
            // transportsize=3 gate on footprint alone, and MinWaterDepth=20
            // -- positive, which is exactly the property the filter under
            // test has to catch since footprint alone would wave it through.
            auto sub = makeDef(false, false, true, "LASER", 200u);
            sub.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 20u, 255u};
            sim.unitDefinitions["ARMSUB"] = sub;
        }

        /**
         * ARMTSHIP "Hulk": 919 metal, 6x6, MinWaterDepth=12,
         * transportsize=3, transportcapacity=20 -- the verified shipped
         * data from rev31.gp3, not an invented number. totala1.hpi's copy
         * says transportmaxunits=20 instead, the 1.0 key; the loader reads
         * that as the same 20 (transportCapacityFromFbi, issue #199).
         */
        void defineSeaTransport(GameSimulation& sim)
        {
            auto ship = makeDef(false, false, true, "", 100u);
            ship.canFly = false;
            ship.floater = true;
            ship.transportCapacity = 20u;
            ship.transportSize = 3u;
            ship.buildCostMetal = Metal(919.0f);
            ship.movementCollisionInfo = UnitDefinition::AdHocMovementClass{6u, 6u, 255u, 255u, 12u, 255u};
            sim.unitDefinitions["ARMTSHIP"] = ship;
        }

        /** ARMATLAS "Atlas", the air lift: capacity one, whatever the FBI says. */
        void defineAirTransport(GameSimulation& sim)
        {
            auto atlas = makeDef(false, false, true, "", 100u);
            atlas.canFly = true;
            atlas.transportCapacity = 1u;
            atlas.transportSize = 3u;
            sim.unitDefinitions["ARMATLAS"] = atlas;
        }

        /**
         * An anti-air picket that cannot touch the ground. Its weapon reaches
         * aircraft but deals no DEFAULT damage, which is what lets a test tell
         * the carrier's field from the cargo's: the anti-ground layer sums
         * every weapon's DEFAULT damage without asking whether it can target
         * ground at all, so a flak gun with ordinary damage would show up on
         * both layers and the two questions would be indistinguishable.
         */
        void defineFlak(GameSimulation& sim)
        {
            WeaponDefinition flak{};
            flak.toAirWeapon = true;
            // Short enough to cover only the tile it stands on: the
            // anti-air layer spreads a gun over ceil(range / cellSize) cells,
            // and a range that reaches the next candidate would make the
            // whole bank quiet and pin nothing.
            flak.maxRange = 30_ss;
            flak.reloadTime = 1_ss;
            flak.burst = 1;
            flak.damage["DEFAULT"] = 0u;
            sim.weaponDefinitions["FLAK"] = flak;

            auto turret = makeDef(false, false, true, "FLAK", 200u);
            sim.unitDefinitions["FLAKBAT"] = turret;
        }

        /**
         * Two shores far enough apart that the walk-back has candidates to
         * choose between: the west bank is heightmap x in [0, 10), the east
         * is x >= 38, so east-bank ground runs from world 96 to 512 -- three
         * 48-unit steps' worth for a search walking back from a target at the
         * far edge. A wider bank than makeWideChannelTerrain's is the point:
         * with one dry candidate "quietest" and "nearest" are the same pick
         * and a test pins nothing.
         */
        MapTerrain makeWideFarShoreTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 0; x < 10; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(90));
                }
                for (int x = 38; x < 64; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(90));
                }
            }
            return MapTerrain(std::move(heights), 60_ss);
        }

        template <typename Order>
        std::vector<Order> ordersFor(const std::vector<PlayerCommand>& commands, UnitId unit)
        {
            std::vector<Order> found;
            for (const auto& c : commands)
            {
                auto unitCommand = std::get_if<PlayerUnitCommand>(&c);
                if (!unitCommand || unitCommand->unit != unit)
                {
                    continue;
                }
                if (auto issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unitCommand->command))
                {
                    if (auto order = std::get_if<Order>(&issue->order))
                    {
                        found.push_back(*order);
                    }
                }
            }
            return found;
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
            unit.buildTimeCompleted = sim.unitDefinitions.at(type).buildTime;
            return unitId;
        }

        void runTicks(GameSimulation& sim, AiPlayerController& ai, int ticks, std::vector<PlayerCommand>& out)
        {
            for (int i = 0; i < ticks; ++i)
            {
                sim.tick();
                ai.tick(sim, out);
            }
        }
    }

    TEST_CASE("naval reachability is wired up for the skirmish AI", "[ai]")
    {
        auto script = makeEmptyCobScript();

        SECTION("the whole channel becomes home water for a hull, and dry land does not")
        {
            GameSimulation sim(makeWideChannelTerrain(), 0u, 0, 0);
            addPlayer(sim, "human", GamePlayerType::Human, "ARM");
            auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
            defineLandUnits(sim);
            defineSeaTransport(sim);
            auto commanderPosition = SimVector(-300_ss, 60_ss, 0_ss);
            addUnit(sim, "ARMCOM", ai, commanderPosition, script);

            auto mapIntel = analyseMap(sim.terrain, {});
            REQUIRE(mapIntel.valid);
            REQUIRE(mapIntel.character != MapCharacter::Land);

            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, mapIntel);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 5, commands);

            const auto& reach = controller.getReachabilityMap();
            REQUIRE(reach.isNavalValid());
            REQUIRE(reach.isNavalReachable(sim, SimVector(0_ss, 0_ss, 0_ss)));
            REQUIRE(reach.isNavalWalkable(sim, SimVector(0_ss, 0_ss, 0_ss)));
            REQUIRE_FALSE(reach.isNavalReachable(sim, commanderPosition));
            REQUIRE_FALSE(reach.isNavalWalkable(sim, commanderPosition));

            // Ground reachability keeps working exactly as before, on its
            // own independent layer.
            REQUIRE(reach.isValid());
            REQUIRE(reach.isReachable(sim, commanderPosition));
        }

        SECTION("a map with no navigable water never floods the naval layer")
        {
            GameSimulation sim(makeDryTerrain(), 0u, 0, 0);
            addPlayer(sim, "human", GamePlayerType::Human, "ARM");
            auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
            defineLandUnits(sim);
            defineSeaTransport(sim);
            addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 60_ss, 0_ss), script);

            auto mapIntel = analyseMap(sim.terrain, {});
            REQUIRE(mapIntel.character == MapCharacter::Land);

            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, mapIntel);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 5, commands);

            REQUIRE_FALSE(controller.getReachabilityMap().isNavalValid());
        }

        SECTION("with no floating hull of any kind in the game data, the naval layer never floods either")
        {
            GameSimulation sim(makeWideChannelTerrain(), 0u, 0, 0);
            addPlayer(sim, "human", GamePlayerType::Human, "ARM");
            auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
            defineLandUnits(sim);
            // Deliberately no defineSeaTransport() and nothing else that
            // floats: ARMTSHIP, ARMROY, ARMSUB (as a side unit, it is
            // present as a unit definition above but only for the passenger
            // filter tests -- resolveAiSideUnits would still find it here,
            // so this section defines the water-needing unit under a name
            // the side lookup does not know, to keep the "no hull" premise
            // honest).
            sim.unitDefinitions.erase("ARMSUB");
            addUnit(sim, "ARMCOM", ai, SimVector(-300_ss, 60_ss, 0_ss), script);

            auto mapIntel = analyseMap(sim.terrain, {});
            REQUIRE(mapIntel.character != MapCharacter::Land);

            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, mapIntel);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 5, commands);

            REQUIRE_FALSE(controller.getReachabilityMap().isNavalValid());
        }
    }

    TEST_CASE("the ground layer homes on the base, not on the tile the commander spawned on", "[ai]")
    {
        // The commander wades where the constructor the ground layer is
        // labelled for cannot -- TANKDS2 to depth 100 against TANKSH2's 12 --
        // so it walks off its spawn island and builds the whole base on ground
        // no kbot it produces can ever leave. baseAnchor is homePosition, which
        // EconomyManager sets the first tick it sees the commander and never
        // revises, so the layer stayed homed on the island the AI abandoned,
        // and every unit it owned read as unreachable from it.
        //
        // Measured on Hundred Isles before this: anchor at 2128,-1600, the AI's
        // own lab at 592,-1216, both factories unreachable on this layer and
        // the lab reachable on the commander's; 168,076 passenger refusals over
        // six games, every one of them this gate, and not one ferry of either
        // kind ever started. After: ferries start and complete.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeWideChannelTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineLandUnits(sim);
        defineSeaTransport(sim);

        // Only the commander may cross: the channel is 30 deep, and makeDef
        // leaves every other land unit unable to wade at all. This is the
        // asymmetry the whole defect rests on, so it is made explicit rather
        // than inherited.
        sim.unitDefinitions.at("ARMCOM").movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 100u};

        auto commanderPosition = SimVector(-300_ss, 60_ss, 0_ss);
        addUnit(sim, "ARMCOM", ai, commanderPosition, script);
        auto mapIntel = analyseMap(sim.terrain, {});

        SECTION("a base across water the constructor cannot cross becomes home")
        {
            auto labPosition = SimVector(400_ss, 60_ss, 0_ss);
            addUnit(sim, "ARMLAB", ai, labPosition, script);

            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, mapIntel);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 5, commands);

            const auto& reach = controller.getReachabilityMap();
            REQUIRE(reach.isValid());
            // Without the re-homing this is false, and with it false every
            // unit built here is refused a lift for the rest of the game.
            REQUIRE(reach.isReachable(sim, labPosition));

            // And it says so out loud. The layer is not the only thing that
            // wants to know where home moved to: anything asking "which way
            // is home" -- a landing search walks back along exactly that --
            // gets the wrong direction off baseAnchor, which still names the
            // spawn tile.
            // Compared by distance rather than by ==: Catch2 stringifies a
            // failed comparison and SimVector has no stream operator.
            REQUIRE(controller.getBlackboard().groundAnchor.has_value());
            REQUIRE(controller.getBlackboard().groundAnchor->distanceSquared(labPosition) == 0_ss);
            REQUIRE(controller.getBlackboard().baseAnchor->distanceSquared(commanderPosition) == 0_ss);
        }

        SECTION("a base on the anchor's own island leaves the homing alone")
        {
            // The fallback fires only when NOT ONE factory is reachable, so on
            // an ordinary map it must not fire at all: the far bank stays
            // unreachable, which is what tells hasUnreachableGround and
            // enemyAcrossWater there is water in the way.
            auto labPosition = SimVector(-400_ss, 60_ss, 0_ss);
            addUnit(sim, "ARMLAB", ai, labPosition, script);

            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, mapIntel);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 5, commands);

            const auto& reach = controller.getReachabilityMap();
            REQUIRE(reach.isReachable(sim, labPosition));
            REQUIRE(reach.isReachable(sim, commanderPosition));
            REQUIRE_FALSE(reach.isReachable(sim, SimVector(400_ss, 60_ss, 0_ss)));

            // Nothing moved, so home is the anchor and the two agree. A
            // reader of groundAnchor never has to ask which case it is in.
            REQUIRE(controller.getBlackboard().groundAnchor->distanceSquared(commanderPosition) == 0_ss);
        }
    }

    TEST_CASE("the landing search walks back towards the base the army has, not the spawn", "[ai]")
    {
        // The other half of the same defect. navalLandingNear steps from the
        // attack target back along the direction of home looking for a shore
        // to put the cargo on, so the origin decides which side of the target
        // it looks at -- and with baseAnchor still naming an island the AI
        // abandoned in its opening minutes, it sets off the wrong way and the
        // fallback probes with it.
        //
        // Measured on Hundred Isles before this, with the re-homing already
        // in: four 1800s games, 15 ferries started and 6 completed, but 1494
        // "no landing near" refusals -- and 1426 of them the SAME point,
        // 52,-157, asked again every tactical pass for the whole game. The
        // search is not broken in general; the same runs found landings at
        // five other targets.
        //
        // The geometry here is the smallest one that can tell the two origins
        // apart. The commander spawns on the west bank and the base ends up
        // on the east, so groundAnchor and baseAnchor are on opposite sides
        // of the channel; the enemy is back on the west bank, across water
        // from the base, which is what makes a ferry wanted at all.
        //
        // Run against the old origin this test does not merely land somewhere
        // else -- it finds NO landing and dispatches no ferry, which is the
        // reported symptom exactly: a target refused every tactical pass for
        // the length of the game while the transport stands idle. Walking
        // back towards the base that exists carries the drop point east,
        // towards the water the hull is coming over, and the ferry goes.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeWideChannelTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineLandUnits(sim);
        defineSeaTransport(sim);

        // Only the commander may cross, as on the map this came from.
        sim.unitDefinitions.at("ARMCOM").movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 100u};

        auto commanderPosition = SimVector(-400_ss, 60_ss, -400_ss);
        addUnit(sim, "ARMCOM", ai, commanderPosition, script);

        // The base the commander actually built, on the far bank.
        auto labPosition = SimVector(400_ss, 60_ss, 0_ss);
        addUnit(sim, "ARMLAB", ai, labPosition, script);

        auto targetPosition = SimVector(-400_ss, 60_ss, 400_ss);
        addUnit(sim, "ARMSOLAR", human, targetPosition, script);

        auto profile = makeDefaultBrutalProfile();
        profile.scoutCount = 0;
        profile.attackArmySize = 1;

        AiPlayerController controller(ai, profile, 42u, analyseMap(sim.terrain, {}));
        std::vector<PlayerCommand> commands;

        // The army is born where the base is, which is the point: every unit
        // the AI owns stands on the bank the spawn anchor knows nothing about.
        addUnit(sim, "ARMPW", ai, SimVector(360_ss, 60_ss, 0_ss), script);
        auto shipId = addUnit(sim, "ARMTSHIP", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        runTicks(sim, controller, 90, commands);
        REQUIRE(controller.getBlackboard().groundAnchor->distanceSquared(labPosition) == 0_ss);

        commands.clear();
        runTicks(sim, controller, profile.tacticalTickInterval, commands);

        auto unloads = ordersFor<UnloadOrder>(commands, shipId);
        REQUIRE(!unloads.empty());
        for (const auto& u : unloads)
        {
            // Walking back towards the spawn is due north along the west
            // bank and leaves x exactly where the target is. Walking back
            // towards the base carries it east.
            REQUIRE(u.destination.x > targetPosition.x);
        }
    }

    TEST_CASE("a sea transport ferries the army across water it cannot walk", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeWideChannelTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineLandUnits(sim);
        defineSeaTransport(sim);
        // No ARMATLAS, no air transport of any kind: resolveAiSideUnits
        // leaves sideUnits.airTransport empty, so this is issue #26's own
        // acceptance criterion -- an island/channel map with nothing that
        // flies to lean on.

        auto commanderPosition = SimVector(-300_ss, 60_ss, 0_ss);
        addUnit(sim, "ARMCOM", ai, commanderPosition, script);
        addUnit(sim, "ARMLAB", ai, SimVector(-320_ss, 60_ss, 40_ss), script);
        // The lone known enemy, well onto the far bank -- an unarmed
        // building, so its only effect is to be worth attacking once seen.
        // Far enough past the channel's own edge (world x 192) that the
        // landing search's first few steps land on the enemy's own dry
        // ground rather than stepping straight through the whole channel.
        addUnit(sim, "ARMSOLAR", human, SimVector(450_ss, 60_ss, 0_ss), script);

        auto profile = makeDefaultBrutalProfile();
        profile.scoutCount = 0;
        profile.attackArmySize = 1;

        AiPlayerController controller(ai, profile, 42u, analyseMap(sim.terrain, {}));
        std::vector<PlayerCommand> commands;

        SECTION("land units are booked onto the ship and carried across")
        {
            auto kbot1 = addUnit(sim, "ARMPW", ai, SimVector(-250_ss, 60_ss, 0_ss), script);
            auto kbot2 = addUnit(sim, "ARMPW", ai, SimVector(-250_ss, 60_ss, 40_ss), script);
            auto shipId = addUnit(sim, "ARMTSHIP", ai, SimVector(0_ss, 0_ss, 0_ss), script);

            // Warm up: reach the Attack phase, discover the enemy across the
            // water, and let the transport pass book its first ferry. Both
            // combat units fit on this one ship, so once they are aboard
            // bb.combatUnits (and armySize with it) legitimately drops to
            // zero -- there is no wave left standing outside the ferry --
            // and ArmyManager/StrategicManager cycle the phase back to Boom
            // on the very next pass. That is correct behaviour, not a bug:
            // the ferry itself does not care about phase (tendFerries tends
            // it regardless), so what this test pins is the booking, not a
            // phase snapshot that is only ever true for one tick.
            runTicks(sim, controller, 90, commands);
            const auto& bb = controller.getBlackboard();
            REQUIRE(controller.getTransportManager().getFerries().count(shipId.value) == 1);

            // A clean window exactly one tacticalTickInterval wide always
            // contains exactly one TransportManager pass, whichever pass
            // that turns out to be -- the fresh booking or a retry of it --
            // because both emit the same one-order-per-passenger shape.
            commands.clear();
            runTicks(sim, controller, profile.tacticalTickInterval, commands);

            auto loads = ordersFor<LoadOrder>(commands, shipId);
            REQUIRE(loads.size() == 2);
            std::vector<UnitId> loaded;
            for (const auto& l : loads)
            {
                loaded.push_back(l.target);
            }
            REQUIRE(std::find(loaded.begin(), loaded.end(), kbot1) != loaded.end());
            REQUIRE(std::find(loaded.begin(), loaded.end(), kbot2) != loaded.end());

            // One UnloadOrder per passenger (the task 4 fix): two
            // passengers, two orders, not one order for the whole party.
            auto unloads = ordersFor<UnloadOrder>(commands, shipId);
            REQUIRE(unloads.size() == 2);
            for (const auto& u : unloads)
            {
                // Set down on the far bank, past the channel (which ends at
                // world x 192).
                REQUIRE(u.destination.x > 192_ss);
            }

            REQUIRE(bb.ferryPassengers.count(kbot1.value) == 1);
            REQUIRE(bb.ferryPassengers.count(kbot2.value) == 1);
        }

        SECTION("a passenger that needs water is never booked onto the ship")
        {
            auto kbot = addUnit(sim, "ARMPW", ai, SimVector(-250_ss, 60_ss, 0_ss), script);
            auto sub = addUnit(sim, "ARMSUB", ai, SimVector(-250_ss, 60_ss, 40_ss), script);
            auto shipId = addUnit(sim, "ARMTSHIP", ai, SimVector(0_ss, 0_ss, 0_ss), script);

            runTicks(sim, controller, 90, commands);
            const auto& bb = controller.getBlackboard();
            // No phase assertion here, for the reason the section above
            // spells out: once a passenger is booked, isFerryPassenger takes
            // it out of combatUnits, armySize follows it down, and
            // StrategicManager cycles the phase straight back to Boom. The
            // Attack phase is true for about one tick and is not something a
            // test can stand on.
            //
            // It used to pass by accident. ARMSUB was counted in combatUnits
            // and padded armySize while the kbot rode the ferry; classifying
            // submarines as hulls (navalCombatUnits) removed that padding and
            // exposed the assertion for what it was. What this section is
            // actually for -- that a unit needing water is never booked --
            // is pinned by the load orders below, which also prove the ferry
            // ran at all.

            commands.clear();
            runTicks(sim, controller, profile.tacticalTickInterval, commands);

            auto loads = ordersFor<LoadOrder>(commands, shipId);
            REQUIRE(!loads.empty());
            for (const auto& l : loads)
            {
                REQUIRE(l.target == kbot);
            }
            REQUIRE(bb.ferryPassengers.count(kbot.value) == 1);
            REQUIRE(bb.ferryPassengers.count(sub.value) == 0);

            // Exactly the booked passenger's worth of unloads -- one, here.
            auto unloads = ordersFor<UnloadOrder>(commands, shipId);
            REQUIRE(unloads.size() == 1);
        }

        SECTION("the passengers gather at one point on the shore, and the hull loads them from the water beside it")
        {
            // Issue #194. Since #193 a passenger no longer wades out to its
            // transport, so a hull sent to each one where it stands is sent
            // at dry land it cannot reach.
            auto kbot1 = addUnit(sim, "ARMPW", ai, SimVector(-300_ss, 60_ss, -60_ss), script);
            auto kbot2 = addUnit(sim, "ARMPW", ai, SimVector(-300_ss, 60_ss, 60_ss), script);
            auto shipId = addUnit(sim, "ARMTSHIP", ai, SimVector(0_ss, 0_ss, 0_ss), script);

            runTicks(sim, controller, 90, commands);
            const auto& bb = controller.getBlackboard();
            REQUIRE(controller.getTransportManager().getFerries().count(shipId.value) == 1);
            const auto& ferry = controller.getTransportManager().getFerries().at(shipId.value);
            REQUIRE(ferry.muster.has_value());
            REQUIRE(ferry.station.has_value());

            // The muster is dry home ground on the near bank, within a
            // crane's reach of the channel (whose near edge is x -192)...
            REQUIRE(ferry.muster->x < -192_ss);
            REQUIRE(ferry.muster->x > -192_ss - 144_ss);
            // ...and the station is in the channel beside it.
            REQUIRE(ferry.station->x > -192_ss);
            REQUIRE(ferry.station->x < 192_ss);
            auto dx = ferry.muster->x - ferry.station->x;
            auto dz = ferry.muster->z - ferry.station->z;
            REQUIRE((dx * dx) + (dz * dz) <= 144_ss * 144_ss);

            commands.clear();
            runTicks(sim, controller, profile.tacticalTickInterval, commands);

            // Both passengers walk to the same point...
            for (auto kbot : {kbot1, kbot2})
            {
                auto moves = ordersFor<MoveOrder>(commands, kbot);
                REQUIRE(!moves.empty());
                REQUIRE((moves.back().destination == *ferry.muster));
            }
            // ...the hull sails to the water beside it, and then loads both.
            auto shipMoves = ordersFor<MoveOrder>(commands, shipId);
            REQUIRE(!shipMoves.empty());
            REQUIRE((shipMoves.back().destination == *ferry.station));
            REQUIRE(ordersFor<LoadOrder>(commands, shipId).size() == 2);

            // And an army still waiting on the lift rallies there too.
            REQUIRE(bb.ferryMuster.has_value());
            REQUIRE((*bb.ferryMuster == *ferry.muster));
            REQUIRE(bb.rallyPoint.has_value());
            REQUIRE((*bb.rallyPoint == *bb.ferryMuster));
        }
    }

    TEST_CASE("a ferry that delivers after the timeout is complete, not overdue", "[ai]")
    {
        // The timeout is for a pickup that never happened. It used to fire on
        // any empty transport past the deadline, so a trip of a little over
        // two minutes that had loaded everyone, crossed and set them down was
        // logged "overdue" at the moment it succeeded (issue #194).
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeWideChannelTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineLandUnits(sim);
        defineSeaTransport(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(-300_ss, 60_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-320_ss, 60_ss, 40_ss), script);
        addUnit(sim, "ARMSOLAR", human, SimVector(450_ss, 60_ss, 0_ss), script);
        auto kbot = addUnit(sim, "ARMPW", ai, SimVector(-250_ss, 60_ss, 0_ss), script);
        auto shipId = addUnit(sim, "ARMTSHIP", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultBrutalProfile();
        profile.scoutCount = 0;
        profile.attackArmySize = 1;
        profile.ferryTimeoutSeconds = 1;
        sim.eventLog.setRecording(true);

        AiPlayerController controller(ai, profile, 42u, analyseMap(sim.terrain, {}));
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 90, commands);
        REQUIRE(controller.getTransportManager().getFerries().count(shipId.value) == 1);

        // Aboard, well past the one-second deadline: the harness never
        // carries orders out, so the pickup is done by hand.
        sim.getUnitState(shipId).carriedUnits.push_back(kbot);
        sim.getUnitState(kbot).carriedBy = shipId;
        runTicks(sim, controller, profile.tacticalTickInterval, commands);
        REQUIRE(controller.getTransportManager().getFerries().at(shipId.value).loaded);
        // The one-second deadline has already called off the unloaded
        // bookings of the warm-up; only what happens from here counts.
        sim.eventLog.clear();

        // And set down again on the far side.
        sim.getUnitState(shipId).carriedUnits.clear();
        sim.getUnitState(kbot).carriedBy = std::nullopt;
        sim.getUnitState(kbot).position = SimVector(300_ss, 60_ss, 0_ss);
        sim.getUnitState(shipId).orders.clear();
        runTicks(sim, controller, profile.tacticalTickInterval, commands);
        REQUIRE(controller.getTransportManager().getFerries().count(shipId.value) == 0);

        auto path = std::filesystem::temp_directory_path() / "rwe-ferry-complete-test.jsonl";
        sim.eventLog.write(path);
        std::ifstream in(path);
        std::string line;
        int complete = 0;
        int overdue = 0;
        while (std::getline(in, line))
        {
            if (line.find("\"transport_ferry\"") == std::string::npos)
            {
                continue;
            }
            complete += line.find("\"why\":\"complete\"") != std::string::npos ? 1 : 0;
            overdue += line.find("\"why\":\"overdue\"") != std::string::npos ? 1 : 0;
        }
        in.close();
        std::filesystem::remove(path);
        REQUIRE(complete == 1);
        REQUIRE(overdue == 0);
    }

    TEST_CASE("with the muster switched off, the hull goes to each passenger where it stands", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeWideChannelTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineLandUnits(sim);
        defineSeaTransport(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(-300_ss, 60_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-320_ss, 60_ss, 40_ss), script);
        addUnit(sim, "ARMSOLAR", human, SimVector(450_ss, 60_ss, 0_ss), script);
        auto kbot = addUnit(sim, "ARMPW", ai, SimVector(-250_ss, 60_ss, 0_ss), script);
        auto shipId = addUnit(sim, "ARMTSHIP", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultBrutalProfile();
        profile.scoutCount = 0;
        profile.attackArmySize = 1;
        profile.seaFerryMuster = false;

        AiPlayerController controller(ai, profile, 42u, analyseMap(sim.terrain, {}));
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 90, commands);
        const auto& ferry = controller.getTransportManager().getFerries().at(shipId.value);
        REQUIRE_FALSE(ferry.muster.has_value());
        REQUIRE_FALSE(controller.getBlackboard().ferryMuster.has_value());

        commands.clear();
        runTicks(sim, controller, profile.tacticalTickInterval, commands);
        REQUIRE(ordersFor<MoveOrder>(commands, kbot).empty());
        REQUIRE(ordersFor<MoveOrder>(commands, shipId).empty());
        REQUIRE(ordersFor<LoadOrder>(commands, shipId).size() == 1);
    }

    TEST_CASE("a landing is scored against what the carrier is, not what the cargo is", "[ai]")
    {
        // Issue #228. Both landing searches used to score every candidate with
        // antiGroundInRadius, which for an air lift is the wrong question -- an
        // Atlas is killed by anti-air, not by guns that cannot reach it. A
        // beach with a flak battery over it and nothing else scored as
        // perfectly quiet and the lift flew into it.
        //
        // The picket here can reach aircraft and deals no ground damage, so
        // the two fields disagree: the carrier's field says "one gun covers
        // this spot", the cargo's says "nothing can hurt you here". An air
        // lift must walk past it; a hull, which the gun cannot touch, must
        // not move at all.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeWideFarShoreTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineLandUnits(sim);
        defineSeaTransport(sim);
        defineAirTransport(sim);
        defineFlak(sim);

        // Only the commander may cross; a kbot stops at depth 20 and the
        // channel is 60 deep, so the east bank is a real barrier and a ferry
        // is wanted.
        sim.unitDefinitions.at("ARMCOM").movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 100u};

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-440_ss, 90_ss, 40_ss), script);
        for (auto z : {0_ss, 24_ss, 48_ss, 72_ss})
        {
            addUnit(sim, "ARMPW", ai, SimVector(-420_ss, 90_ss, z), script);
        }
        // The objective at the far edge, unarmed so it is only a place to go.
        addUnit(sim, "ARMSOLAR", human, SimVector(450_ss, 90_ss, 0_ss), script);
        // The picket exactly on the walk-back's first dry step: 450 less
        // 4 * 48 is 258, and 258 is east-bank ground.
        auto flakId = addUnit(sim, "FLAKBAT", human, SimVector(258_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultBrutalProfile();
        profile.cheatModeOmniscient = true;
        profile.scoutCount = 0;
        profile.attackArmySize = 1;
        profile.tacticalTickInterval = 1;
        profile.attackInWaves = false;
        // See the sea case above for why the phase must stay in Attack and
        // why the enemy has to be outside defendRadius.
        profile.retreatArmySize = 0;
        profile.defendRadius = 200_ss;
        // antiGroundInRadius is a flat box sum, so at the shipped 300 a picket
        // a hundred units away counts the same as one underfoot. One cell
        // isolates the picket's own tile from the objective's.
        profile.ferryLandingThreatRadius = 16.0f;

        SECTION("the air lift will not be put down under the anti-air")
        {
            auto atlasId = addUnit(sim, "ARMATLAS", ai, SimVector(-400_ss, 150_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, analyseMap(sim.terrain, {}));
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 90, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.knownEnemies.count(flakId.value) == 1);
            REQUIRE(bb.phase == GamePhase::Attack);
            REQUIRE(bb.enemyAcrossWater);
            REQUIRE(controller.getThreatMap().antiAirCoverAt(SimVector(258_ss, 90_ss, 0_ss)) >= 1.0f);

            auto unloads = ordersFor<UnloadOrder>(commands, atlasId);
            REQUIRE(!unloads.empty());
            auto drop = unloads.front().destination;
            // The carrier's own field, which is the fix: nothing can shoot at
            // an aircraft where it is being set down. This is the whole
            // assertion. Before the field was chosen by carrier, anti-ground
            // was flat -- the picket deals no ground damage -- so the search
            // took the quiet point nearest home, which is the walk-back's
            // first step and sits in the picket's anti-air cover.
            REQUIRE(controller.getThreatMap().antiAirCoverAt(drop) == 0.0f);
            // Said as a distance too, so the failure reads as "put down under
            // the gun" rather than as a bare nought.
            auto gun = SimVector(258_ss, 90_ss, 0_ss);
            REQUIRE(drop.distanceSquared(gun) >= SimScalar(48.0f * 48.0f));
        }

        SECTION("a hull ignores anti-air it cannot be hurt by")
        {
            auto shipId = addUnit(sim, "ARMTSHIP", ai, SimVector(0_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, analyseMap(sim.terrain, {}));
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 90, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.knownEnemies.count(flakId.value) == 1);
            REQUIRE(bb.phase == GamePhase::Attack);
            REQUIRE(bb.enemyAcrossWater);
            REQUIRE(std::find(bb.transports.begin(), bb.transports.end(), shipId) != bb.transports.end());

            auto unloads = ordersFor<UnloadOrder>(commands, shipId);
            REQUIRE(!unloads.empty());
            // Still the nearest quiet point, 258: for a surface hull the
            // carrier's field is anti-ground, and this gun cannot touch it.
            REQUIRE(unloads.front().destination.x > 240_ss);
        }
    }
}
