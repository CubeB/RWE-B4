#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <memory>
#include <optional>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_encoders.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/pathfinding/UnitPath.h>
#include <rwe/puppet/TadPuppetDriver.h>
#include <rwe/puppet/puppet_test_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MixedOwnership.h>
#include <rwe/sim/SimulationOwnership.h>
#include <rwe/sim/TaLiveBatch.h>
#include <rwe/sim/TaLiveSender.h>
#include <rwe/sim/TaPeerIds.h>
#include <rwe/sim/TaWireTape.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/sim_test_util.h>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        /**
         * The world a live sender is exercised in: the puppet fixture's walker,
         * builder and product, plus a walker with a gun and one that cannot
         * fight. The first three carry the walk and the build; the last two are
         * what the 0x0d and the 0x0b need, and neither has a use for the frame
         * that the other fixtures build.
         */
        void defineLiveWorld(GameSimulation& sim)
        {
            definePuppetTestWorld(sim);

            auto gun = makePuppetWalkerDef();
            gun.canAttack = true;
            gun.weapon1 = "LASER";
            gun.standingFireOrder = UnitFireOrders::FireAtWill;
            gun.category = "ARM ALL";
            gun.sightDistance = 512u;
            sim.unitDefinitions["GUN"] = gun;
            sim.unitScriptDefinitions["GUN"] = *makeEmptyCobScript({"base"});

            auto victim = makePuppetWalkerDef();
            victim.canAttack = false;
            victim.shootMe = true;
            victim.category = "CORE ALL";
            sim.unitDefinitions["VICTIM"] = victim;
            sim.unitScriptDefinitions["VICTIM"] = *makeEmptyCobScript({"base"});

            WeaponDefinition laser{};
            laser.physicsType = ProjectilePhysicsTypeLineOfSight();
            laser.maxRange = 600_ss;
            laser.reloadTime = SimScalar(0.2f);
            laser.burst = 1;
            laser.velocity = 960_ss / 30_ss;
            laser.damageRadius = 16_ss;
            laser.damage["DEFAULT"] = 30;
            laser.turret = true;
            laser.tolerance = SimAngle(1000);
            laser.pitchTolerance = SimAngle(1000);
            // The TDF's own `ID`, which the 0x0d carries and the peer indexes
            // its weapon table by.
            laser.taWeaponId = 42u;
            sim.weaponDefinitions["LASER"] = laser;
        }

        std::vector<std::string> liveLoadOrder()
        {
            return {"GUN", "KBOT", "SOLAR", "TANK", "VICTIM"};
        }

        std::vector<bool> liveCanFly()
        {
            return {false, false, false, false, false};
        }

        /**
         * A sim with one Local and one Remote player and a sender watching the
         * Local one. There is a Remote player because that is the game this
         * sender exists for: the mixed-ownership outbox only records anything
         * when there is a peer to record it for.
         */
        struct LiveGame
        {
            GameSimulation sim;
            PlayerId local;
            PlayerId remote;
            uint16_t maxUnits;

            explicit LiveGame(uint16_t maxUnits = 8) : sim(makeFlatTerrain(64, 64), 0u, 0, 0), maxUnits(maxUnits)
            {
                sim.lineOfSightMode = LineOfSightMode::Circular;
                local = addPlayer(sim, "local");
                remote = addPlayer(sim, "remote");
                defineLiveWorld(sim);
                markRemote();
            }

            /** The Remote player's units are the peer's to decide, and its hits are the peer's to report. */
            void markRemote()
            {
                sim.getPlayer(remote).simulation = PlayerSimulation::Remote;
            }

            UnitId spawn(const std::string& type, PlayerId owner, const SimVector& position)
            {
                auto id = sim.trySpawnCompletedUnit(type, owner, position, std::nullopt);
                REQUIRE(id.has_value());
                return *id;
            }

            uint16_t peerIdOf(unsigned int index) const
            {
                return tadUnitIdOfIndex(1, static_cast<uint16_t>(index), maxUnits);
            }

            /**
             * The peer's answers stand in for what the puppet driver learns
             * from the stream: a Remote unit's id in the peer's own block, and
             * the peer's DirectPlay id from its status message. TA hands the
             * host block zero and the joiner the next one, and a record naming
             * a peer's unit has to use the peer's number.
             */
            void nameOnPeer(UnitId unit, unsigned int index)
            {
                peerIdsByUnit[unit] = peerIdOf(index);
            }

            /**
             * Registers a sender, which is the whole of the wiring: the
             * simulation is told where to call, and the sender stays ours.
             */
            void attach()
            {
                TaLiveSenderSettings settings;
                settings.sender = local;
                settings.maxUnits = maxUnits;
                settings.unitLoadOrder = liveLoadOrder();

                TaPeerIds peerIds;
                peerIds.unitId = [this](UnitId unit) -> std::optional<uint16_t> {
                    auto it = peerIdsByUnit.find(unit);
                    if (it == peerIdsByUnit.end())
                    {
                        return std::nullopt;
                    }
                    return it->second;
                };
                peerIds.dplayId = [](PlayerId) -> std::optional<uint32_t> { return peerDplayId; };

                sender = std::make_unique<TaLiveSender>(sim, settings, std::move(peerIds));
                sim.setTaLiveSender(sender->hooks());
            }

            void detach()
            {
                sim.setTaLiveSender(TaLiveSenderHooks{});
            }

            std::unique_ptr<TaLiveSender> sender;

            /** The DirectPlay id the peer's status message carries. */
            static constexpr uint32_t peerDplayId = 7u;

            /**
             * Ticks the game and takes every batch the sender offers, which is
             * what the network layer does a few ticks late: six of them to a
             * message, and a message is somebody else's framing.
             */
            std::map<UnitId, uint16_t> peerIdsByUnit;

            std::vector<TaLiveBatch> runAndTake(int ticks)
            {
                std::vector<TaLiveBatch> batches;
                for (int i = 0; i < ticks; ++i)
                {
                    sim.tick();
                    while (auto batch = sender->takeBatch())
                    {
                        batches.push_back(std::move(*batch));
                    }
                }
                return batches;
            }

            TadUnitStateLayout layout() const
            {
                return tadUnitStateLayout(liveCanFly(), maxUnits);
            }
        };

        /** Every subpacket of one code across every batch, in wire order. */
        std::vector<TadBytes> subPacketsOf(const std::vector<TaLiveBatch>& batches, TadSubPacketCode code)
        {
            std::vector<TadBytes> found;
            for (const auto& batch : batches)
            {
                for (const auto& subPacket : batch.subPackets)
                {
                    if (!subPacket.empty() && static_cast<TadSubPacketCode>(subPacket[0]) == code)
                    {
                        found.push_back(subPacket);
                    }
                }
            }
            return found;
        }

        /** One tick's 0x2c decoded, which is the tick's clock and its full-state record. */
        std::optional<TadUnitState> unitStateOf(const TaLiveBatch& batch, const TadUnitStateLayout& layout)
        {
            for (const auto& subPacket : batch.subPackets)
            {
                if (subPacket.size() > 7 && subPacket[0] == 0x2c)
                {
                    return tadDecodeUnitState(subPacket, layout);
                }
            }
            return std::nullopt;
        }

        std::vector<TadUnitState> unitStatesOf(const std::vector<TaLiveBatch>& batches, const TadUnitStateLayout& layout)
        {
            std::vector<TadUnitState> states;
            for (const auto& batch : batches)
            {
                if (auto state = unitStateOf(batch, layout))
                {
                    states.push_back(std::move(*state));
                }
            }
            return states;
        }

        /**
         * Where RWE's world puts the map's top left, which is TA's origin: half
         * the map's extent, in whole world units for a waypoint and in 16.16
         * for a position.
         */
        int halfWidthUnits(const GameSimulation& sim)
        {
            return static_cast<int>(simScalarToFloat(sim.terrain.getWidthInWorldUnits() / 2_ss));
        }

        int halfHeightUnits(const GameSimulation& sim)
        {
            return static_cast<int>(simScalarToFloat(sim.terrain.getHeightInWorldUnits() / 2_ss));
        }

        /** The global wire id a unit in the Local player's block was given. */
        uint16_t localIdOf(std::size_t index)
        {
            return tadUnitIdOfIndex(0, static_cast<uint16_t>(index), 1000);
        }
    }

    TEST_CASE("a live sender's batch decodes as a walk, a build, a shot and a settle", "[talive]")
    {
        LiveGame game;
        game.attach();

        auto tank = game.spawn("TANK", game.local, SimVector(0_ss, 0_ss, 0_ss));
        auto tankStart = game.sim.getUnitState(tank).position;
        auto kbot = game.spawn("KBOT", game.local, SimVector(-60_ss, 0_ss, 0_ss));
        game.spawn("GUN", game.local, SimVector(200_ss, 0_ss, 200_ss));
        auto victim = game.spawn("VICTIM", game.remote, SimVector(200_ss, 0_ss, 236_ss));
        game.nameOnPeer(victim, 0);
        game.sim.getUnitState(tank).orders.push_back(MoveOrder(SimVector(300_ss, 0_ss, 300_ss)));

        // A frame, so the 0x09 and the 0x12 have a pair to carry. The sender
        // hears about the build from the same call the recorder does, which is
        // what sharing the encoding rather than copying it buys; the frame is
        // finished by hand because a frame nobody is building rots away. It
        // stands clear of the tank's path, because a walker that meets a
        // building's cells is pushed aside and this is a test of the wire.
        auto solar = game.sim.trySpawnUnit("SOLAR", game.local, SimVector(40_ss, 0_ss, -120_ss), std::nullopt);
        REQUIRE(solar);
        game.sender->buildStarted(game.sim, kbot, *solar);

        auto batches = game.runAndTake(5);
        game.sim.getUnitState(*solar).finishBuilding(game.sim.unitDefinitions.at("SOLAR"));
        auto rest = game.runAndTake(195);
        batches.insert(batches.end(), rest.begin(), rest.end());
        REQUIRE(batches.size() == 200);

        // A 0x09 names the type by load-order index, the frame by its own id,
        // and where it was put down in TA's coordinates.
        auto builds = subPacketsOf(batches, TadSubPacketCode::UnitBuildStarted);
        REQUIRE(builds.size() == 1);
        auto build = tadDecodeBuildStarted(builds[0]);
        REQUIRE(build);
        REQUIRE(build->typeIndex == 3u);
        REQUIRE(build->unitId == localIdOf(3));
        auto halfWidth = simScalarToFixed(SimScalar(halfWidthUnits(game.sim)));
        auto halfHeight = simScalarToFixed(SimScalar(halfHeightUnits(game.sim)));
        REQUIRE(build->position.x == simScalarToFixed(40_ss) + halfWidth);
        REQUIRE(build->position.z == simScalarToFixed(-120_ss) + halfHeight);

        // The gun fires at the Remote unit standing next to it, and the shot
        // names the shooter by our own id and the target by the peer's.
        auto shots = subPacketsOf(batches, TadSubPacketCode::WeaponFired);
        REQUIRE_FALSE(shots.empty());
        auto shot = tadDecodeShot(shots.front());
        REQUIRE(shot);
        REQUIRE(shot->shooterId == localIdOf(2));
        REQUIRE(shot->targetId == game.peerIdOf(0));
        REQUIRE(shot->weaponSlot == 0u);
        // The first word is the weapon's TDF `ID`, not an angle: the peer
        // looks its flags up by it and a shot naming another weapon is flown
        // by that one or dropped.
        REQUIRE(shot->rotation.x == 42);

        // A waypoint entry goes out when the path changes, carries the path in
        // TA's coordinates, and its waypoints are corners of the map rather
        // than RWE's own frame.
        auto states = unitStatesOf(batches, game.layout());
        REQUIRE(states.size() == 200);
        REQUIRE(states[0].tick == 1u);

        int mapWidth = 2 * halfWidthUnits(game.sim);
        int mapHeight = 2 * halfHeightUnits(game.sim);
        bool sawTankPath = false;
        for (const auto& state : states)
        {
            // Every 0x2c ends with the record for the slot the tick names, and
            // that is the record that keeps the unit alive on the peer.
            REQUIRE(state.sync);
            REQUIRE(state.sync->index == state.tick % game.maxUnits);

            for (const auto& update : state.updates)
            {
                const auto* ground = std::get_if<TadGroundPath>(&update.mover);
                REQUIRE(ground);
                if (update.index != 0u || ground->waypoints.empty())
                {
                    continue;
                }
                sawTankPath = true;
                for (const auto& waypoint : ground->waypoints)
                {
                    REQUIRE(waypoint.x >= 0);
                    REQUIRE(waypoint.x < mapWidth);
                    REQUIRE(waypoint.z >= 0);
                    REQUIRE(waypoint.z < mapHeight);
                }
            }
        }
        REQUIRE(sawTankPath);

        // A kill on the peer's side is the 0x0b the outbox recorded, and the
        // frame's completion is the 0x12 naming the builder.
        REQUIRE_FALSE(subPacketsOf(batches, TadSubPacketCode::UnitTakeDamage).empty());
        auto finished = subPacketsOf(batches, TadSubPacketCode::UnitBuildFinished);
        REQUIRE(finished.size() == 1);
        auto completion = tadDecodeBuildFinished(finished[0]);
        REQUIRE(completion);
        REQUIRE(completion->unitId == localIdOf(3));
        REQUIRE(completion->builderId == localIdOf(1));

        auto walked = game.sim.getUnitState(tank).position != tankStart;
        REQUIRE(walked);
        REQUIRE(findUnitOfType(game.sim, "SOLAR"));
    }

    TEST_CASE("a 0x0d carries the weapon ID, then the bearing and elevation", "[talive]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        defineLiveWorld(sim);
        auto local = addPlayer(sim, "local");
        auto gun = sim.trySpawnCompletedUnit("GUN", local, SimVector(0_ss, 0_ss, 0_ss), std::nullopt);
        REQUIRE(gun);

        TaWireTape tape(sim, TaWireTapeSettings{8, 0, liveLoadOrder()}, std::vector<PlayerId>{local}, TaPeerIds{});

        // Equal parts east and up, none north: a shot at an eighth of
        // elevation, bearing east. The original's three words are the weapon's
        // ID, the bearing and the elevation, in that order; the first is not an
        // angle, and the elevation is third. East is a quarter turn in RWE's
        // heading and minus a quarter in TA's, whose heading faces the other way.
        tape.shotFired(sim, *gun, 0, std::nullopt, SimVector(0_ss, 0_ss, 0_ss), SimVector(1_ss, 1_ss, 0_ss), SimVector(1_ss, 1_ss, 0_ss));
        auto records = tape.endOfTick(sim, local);

        auto it = std::find_if(records.unitPass.begin(), records.unitPass.end(), [](const TadBytes& s) {
            return !s.empty() && static_cast<TadSubPacketCode>(s[0]) == TadSubPacketCode::WeaponFired;
        });
        REQUIRE(it != records.unitPass.end());
        auto shot = tadDecodeShot(*it);
        REQUIRE(shot);
        REQUIRE(shot->rotation.x == 42);
        REQUIRE(shot->rotation.y == -16384);
        REQUIRE(shot->rotation.z == 8192);
    }

    TEST_CASE("a waypoint entry leads with the corner the unit has just left", "[talive]")
    {
        // ta-rwe-test-10.pcap: a real TA sends a new entry as its unit reaches
        // a corner, and the entry begins with that corner, then the one headed
        // for and the one after (TOTALA-EXE.md section 102's wp[0], wp[1],
        // wp[2]). Leading with the corner headed for had TA's copy of RWE's
        // unit cut one corner ahead and be pulled back by every full state.
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        defineLiveWorld(sim);
        auto local = addPlayer(sim, "local");
        auto tank = sim.trySpawnCompletedUnit("TANK", local, SimVector(0_ss, 0_ss, 0_ss), std::nullopt);
        REQUIRE(tank);

        UnitPath unitPath;
        unitPath.waypoints = {
            SimVector(0_ss, 0_ss, 0_ss),
            SimVector(64_ss, 0_ss, 0_ss),
            SimVector(64_ss, 0_ss, 128_ss),
            SimVector(-32_ss, 0_ss, 128_ss)};
        NavigationStateMoving moving;
        moving.movementGoal = unitPath.waypoints.back();
        moving.path = PathFollowingInfo(std::move(unitPath), sim.gameTime);
        auto& unit = sim.getUnitState(*tank);
        unit.navigationState.state = std::move(moving);

        TaWireTape tape(sim, TaWireTapeSettings{8, 0, liveLoadOrder()}, std::vector<PlayerId>{local}, TaPeerIds{});
        auto records = tape.endOfTick(sim, local);
        auto state = tadDecodeUnitState(records.unitState, tadUnitStateLayout(liveCanFly(), 8));
        REQUIRE(state);
        REQUIRE(state->updates.size() == 1);
        const auto* ground = std::get_if<TadGroundPath>(&state->updates[0].mover);
        REQUIRE(ground);
        REQUIRE(ground->waypoints.size() == 3);

        auto halfWidth = halfWidthUnits(sim);
        auto halfHeight = halfHeightUnits(sim);
        REQUIRE(ground->waypoints[0].x == halfWidth);
        REQUIRE(ground->waypoints[0].z == halfHeight);
        REQUIRE(ground->waypoints[1].x == 64 + halfWidth);
        REQUIRE(ground->waypoints[2].z == 128 + halfHeight);
    }

    TEST_CASE("a unit that stops is described by an entry with no waypoints", "[talive]")
    {
        LiveGame game;
        game.attach();
        auto tank = game.spawn("TANK", game.local, SimVector(0_ss, 0_ss, 0_ss));
        game.sim.getUnitState(tank).orders.push_back(MoveOrder(SimVector(300_ss, 0_ss, 300_ss)));

        game.runAndTake(20);
        game.sim.getUnitState(tank).orders.clear();

        // The entry is a delta, so it is the stopping that puts one out, and an
        // entry with no waypoints is how the receiver is told to stop.
        auto stopped = game.runAndTake(1);
        auto states = unitStatesOf(stopped, game.layout());
        REQUIRE(states.size() == 1);
        REQUIRE(states[0].updates.size() == 1);
        const auto* ground = std::get_if<TadGroundPath>(&states[0].updates[0].mover);
        REQUIRE(ground);
        REQUIRE(ground->waypoints.empty());
    }

    TEST_CASE("a 0x28 goes out on the settle cadence and carries the sender's own economy", "[talive]")
    {
        LiveGame game;
        game.attach();
        game.spawn("TANK", game.local, SimVector(0_ss, 0_ss, 0_ss));

        // The settle runs in the tick's phases, so the player's own figures
        // read once the tick is over are the ones its 0x28 carried.
        unsigned int settles = 0;
        for (int i = 0; i < 240; ++i)
        {
            game.sim.tick();
            while (auto batch = game.sender->takeBatch())
            {
                for (const auto& subPacket : batch->subPackets)
                {
                    if (subPacket.size() != 58 || subPacket[0] != 0x28)
                    {
                        continue;
                    }
                    auto stats = tadDecodeResourceStats(subPacket);
                    REQUIRE(stats);
                    const auto& info = game.sim.getPlayer(game.local);
                    REQUIRE(stats->metalStored == info.metal.value);
                    REQUIRE(stats->energyStored == info.energy.value);
                    REQUIRE(stats->metalStorage == info.maxMetal.value);
                    ++settles;
                }
            }
        }

        // The corpus's interval, landing on the settle, and a sender's own
        // state alone rather than a copy per peer.
        REQUIRE(settles == 2);
    }

    TEST_CASE("every Local unit's slot gets a full-state record once a cycle, and no owned slot goes out empty", "[talive]")
    {
        LiveGame game(4);
        game.attach();
        game.spawn("TANK", game.local, SimVector(0_ss, 0_ss, 0_ss));
        game.spawn("KBOT", game.local, SimVector(60_ss, 0_ss, 0_ss));
        game.spawn("GUN", game.local, SimVector(120_ss, 0_ss, 0_ss));
        game.spawn("VICTIM", game.remote, SimVector(180_ss, 0_ss, 0_ss));

        // Two cycles of a four-slot block.
        auto batches = game.runAndTake(8);
        REQUIRE(batches.size() == 8);

        auto states = unitStatesOf(batches, game.layout());
        REQUIRE(states.size() == 8);

        std::map<uint16_t, unsigned int> described;
        std::map<uint16_t, unsigned int> fullHealth;
        for (const auto& state : states)
        {
            REQUIRE(state.sync);
            REQUIRE(state.sync->index == state.tick % game.maxUnits);
            if (state.sync->typeIndex != 0)
            {
                // Never an empty record for a slot this machine has a unit in.
                described[state.sync->index] += 1;
                fullHealth[state.sync->health] += 1;
            }
        }

        // Each of the three Local units is described on its own tick and on
        // no other, and the fourth slot is empty every time, which is true.
        REQUIRE(described.size() == 3);
        REQUIRE(described[0] == 2);
        REQUIRE(described[1] == 2);
        REQUIRE(described[2] == 2);
        REQUIRE(fullHealth.size() == 1);

        // Nothing carries the Remote unit: it is not in our block, so it has
        // no slot, no type index and no waypoint entry anywhere in the stream.
        for (const auto& state : states)
        {
            for (const auto& update : state.updates)
            {
                REQUIRE(update.index < 3u);
            }
        }
    }

    TEST_CASE("a Local unit's death goes out as the 0x0c the outbox recorded", "[talive]")
    {
        LiveGame game;
        auto attacker = game.spawn("GUN", game.remote, SimVector(200_ss, 0_ss, 200_ss));
        auto victim = game.spawn("VICTIM", game.local, SimVector(200_ss, 0_ss, 240_ss));
        game.attach();
        game.nameOnPeer(attacker, 0);
        game.sim.getUnitState(victim).hitPoints = 10;

        applyIncomingDamage(game.sim, victim, attacker, 50);
        REQUIRE(game.sim.getUnitState(victim).isDead());

        // What the simulation decided is what goes out, in the recorder's own
        // packing: the cause in the high nibble and the corpse level below.
        const auto& outbox = mixedOwnershipOutboxOf(game.sim);
        REQUIRE(outbox.deaths.size() == 1);
        REQUIRE(outbox.deaths[0].unit == victim);
        REQUIRE(outbox.deaths[0].killer == attacker);
        auto recorded = outbox.deaths[0];

        auto batches = game.runAndTake(1);
        auto deaths = subPacketsOf(batches, TadSubPacketCode::UnitKilled);
        REQUIRE(deaths.size() == 1);
        auto death = tadDecodeDeath(deaths[0]);
        REQUIRE(death);
        REQUIRE(death->unitId == localIdOf(0));
        REQUIRE(death->killerId == game.peerIdOf(0));
        REQUIRE(death->killerDplayId == LiveGame::peerDplayId);
        REQUIRE(death->causeAndLevel == recorded.causeAndLevel);
        REQUIRE(death->cause() == 1u);
        REQUIRE(death->severity == recorded.severity);

        // Drained, so the same death is not sent again on the next tick.
        REQUIRE(mixedOwnershipOutboxOf(game.sim).deaths.empty());
        REQUIRE(subPacketsOf(game.runAndTake(1), TadSubPacketCode::UnitKilled).empty());
    }

    TEST_CASE("a Local hit on a Remote unit goes out as the 0x0b the outbox recorded", "[talive]")
    {
        LiveGame game;
        auto shooter = game.spawn("GUN", game.local, SimVector(200_ss, 0_ss, 200_ss));
        auto victim = game.spawn("VICTIM", game.remote, SimVector(200_ss, 0_ss, 240_ss));
        game.attach();
        game.nameOnPeer(victim, 0);

        game.sim.applyDamage(victim, 30, shooter);
        REQUIRE(mixedOwnershipOutboxOf(game.sim).damage.size() == 1);

        auto batches = game.runAndTake(1);
        auto hits = subPacketsOf(batches, TadSubPacketCode::UnitTakeDamage);
        REQUIRE(hits.size() == 1);
        auto hit = tadDecodeDamage(hits[0]);
        REQUIRE(hit);
        REQUIRE(hit->victimId == game.peerIdOf(0));
        REQUIRE(hit->attackerId == localIdOf(0));
        REQUIRE(hit->damage == 30u);

        // The outbox is the sender's to drain, and draining it is the whole of
        // its ownership of it: the same hit must not go out twice.
        REQUIRE(mixedOwnershipOutboxOf(game.sim).damage.empty());
        REQUIRE(subPacketsOf(game.runAndTake(1), TadSubPacketCode::UnitTakeDamage).empty());
    }

    TEST_CASE("a hit on a unit the peer has not named yet costs that record and nothing else", "[talive]")
    {
        LiveGame game;
        auto attacker = game.spawn("TANK", game.local, SimVector(200_ss, 0_ss, 200_ss));
        auto victim = game.spawn("VICTIM", game.remote, SimVector(200_ss, 0_ss, 240_ss));

        // No peer ids: the peer's block allocation is not ours to guess at, so
        // the hit cannot be named and the record is dropped rather than
        // written with a victim id of zero, which the peer would read as no
        // unit at all.
        game.attach();
        game.sim.applyDamage(victim, 30, attacker);

        auto batches = game.runAndTake(game.maxUnits);
        REQUIRE(subPacketsOf(batches, TadSubPacketCode::UnitTakeDamage).empty());
        REQUIRE(game.sender->stats().recordsDroppedNoId == 1);

        // The ticks still went out whole: the sender's own units are described
        // whatever the peer has or has not said, and a whole cycle of the block
        // still describes the gun.
        auto states = unitStatesOf(batches, game.layout());
        REQUIRE(states.size() == batches.size());
        unsigned int described = 0;
        for (const auto& state : states)
        {
            REQUIRE(state.sync);
            if (state.sync->typeIndex == 4u)
            {
                ++described;
            }
        }
        REQUIRE(described == batches.size() / game.maxUnits);
    }

    TEST_CASE("an aim script a Local unit starts goes out as a 0x10 with its heading and pitch", "[talive]")
    {
        // ta-rwe-test-7.pcap: TA sent its commander's AimPrimary, function 11
        // of ARMCOM.cob, as 10 <unit> 0b 00 02 <heading> <pitch>, so the
        // peer's puppet turns its torso to fire rather than shooting from
        // whichever way its body faces.
        LiveGame game;
        game.attach();
        auto gun = game.spawn("GUN", game.local, SimVector(0_ss, 0_ss, 0_ss));
        auto victim = game.spawn("VICTIM", game.remote, SimVector(300_ss, 0_ss, 300_ss));

        game.sender->aimScriptStarted(gun, 11, 1234, -56);
        game.sender->aimScriptStarted(victim, 11, 1, 2);
        auto batches = game.runAndTake(1);

        auto calls = subPacketsOf(batches, TadSubPacketCode::UnitStartScript);
        REQUIRE(calls.size() == 1);
        auto call = tadDecodeScriptCall(calls[0]);
        REQUIRE(call);
        REQUIRE(call->unitId == localIdOf(0));
        REQUIRE(call->scriptIndex == 11u);
        REQUIRE(call->argCount == 2u);
        REQUIRE(call->args[0] == 1234);
        REQUIRE(call->args[1] == -56);
    }

    TEST_CASE("nothing is sent for a Remote unit", "[talive]")
    {
        LiveGame game(2);
        game.attach();
        game.spawn("VICTIM", game.remote, SimVector(0_ss, 0_ss, 0_ss));
        auto local = game.spawn("TANK", game.local, SimVector(300_ss, 0_ss, 300_ss));
        game.sim.getUnitState(local).orders.push_back(MoveOrder(SimVector(-300_ss, 0_ss, -300_ss)));

        auto batches = game.runAndTake(6);
        auto states = unitStatesOf(batches, game.layout());
        REQUIRE(states.size() == 6);

        // The Remote unit sits at no slot of ours, so slot 0 is our tank's on
        // the even ticks and empty on the odd ones, and the tank's waypoint
        // entry is the only mover in the stream.
        unsigned int described = 0;
        for (const auto& state : states)
        {
            REQUIRE(state.sync);
            REQUIRE(state.sync->index == state.tick % game.maxUnits);
            if (state.sync->typeIndex != 0)
            {
                ++described;
                REQUIRE(state.sync->index == 0u);
                REQUIRE(state.sync->health == game.sim.getUnitState(local).hitPoints);
            }
            for (const auto& update : state.updates)
            {
                REQUIRE(update.index == 0u);
            }
        }
        REQUIRE(described == 3);
    }

    TEST_CASE("an unregistered sender is called no more, and dropping one is the network layer's business", "[talive]")
    {
        LiveGame game;
        game.attach();
        game.spawn("TANK", game.local, SimVector(0_ss, 0_ss, 0_ss));

        auto before = game.runAndTake(2);
        REQUIRE(before.size() == 2);
        auto sentSoFar = game.sender->stats().batches;
        REQUIRE(sentSoFar == 2);

        // The simulation holds no sender, only where to call, so the order the
        // two are torn down in does not matter -- as long as the table is
        // handed back first.
        game.detach();
        game.sender.reset();

        REQUIRE_FALSE(game.sim.taLiveSender.attached());
        for (int i = 0; i < 5; ++i)
        {
            game.sim.tick();
        }
        REQUIRE_FALSE(game.sender);
    }

    TEST_CASE("a unit that changes hands to the peer is killed with a cause-4 death and stops being described", "[talive]")
    {
        LiveGame game;
        game.attach();
        auto taken = game.spawn("TANK", game.local, SimVector(0_ss, 0_ss, 0_ss));
        game.spawn("KBOT", game.local, SimVector(60_ss, 0_ss, 0_ss));

        game.sim.getUnitState(taken).owner = game.remote;
        game.sender->unitCaptured(game.sim, taken, game.remote);

        auto batches = game.runAndTake(4);
        auto states = unitStatesOf(batches, game.layout());

        // The old id is the only thing the peer can delete the unit with, and
        // the slot it held is empty afterwards because the unit is not ours.
        auto deaths = subPacketsOf(batches, TadSubPacketCode::UnitKilled);
        REQUIRE(deaths.size() == 1);
        auto death = tadDecodeDeath(deaths[0]);
        REQUIRE(death);
        REQUIRE(death->unitId == localIdOf(0));
        REQUIRE(death->cause() == 4u);

        for (const auto& state : states)
        {
            REQUIRE(state.sync);
            if (state.sync->index == 0u)
            {
                REQUIRE(state.sync->typeIndex == 0u);
            }
        }
        REQUIRE(game.sender->stats().unitsRefused == 0);
    }

    TEST_CASE("a unit captured from the peer is described from the next full-state record on", "[talive]")
    {
        LiveGame game(2);
        game.attach();
        game.spawn("TANK", game.local, SimVector(0_ss, 0_ss, 0_ss));
        auto theirs = game.spawn("VICTIM", game.remote, SimVector(300_ss, 0_ss, 300_ss));

        auto before = game.runAndTake(2);
        for (const auto& state : unitStatesOf(before, game.layout()))
        {
            REQUIRE(state.sync);
            auto describedOurs = state.sync->typeIndex == 0u || state.sync->index == 0u;
            REQUIRE(describedOurs);
        }

        game.sim.getUnitState(theirs).owner = game.local;
        game.sender->unitCaptured(game.sim, theirs, game.local);

        // No 0x09: a capture is not a nanoframe, and no cause-4 death either --
        // the old id was never ours to kill. It takes the slot the freed one
        // left, and from then on the round robin describes it.
        auto after = game.runAndTake(2);
        REQUIRE(subPacketsOf(after, TadSubPacketCode::UnitBuildStarted).empty());
        REQUIRE(subPacketsOf(after, TadSubPacketCode::UnitKilled).empty());

        std::map<uint16_t, unsigned int> described;
        for (const auto& state : unitStatesOf(after, game.layout()))
        {
            REQUIRE(state.sync);
            if (state.sync->typeIndex != 0)
            {
                described[state.sync->index] += 1;
            }
        }
        REQUIRE(described.size() == 2);
    }

    TEST_CASE("the sender's batches drive a puppet driver in a second simulation", "[talive]")
    {
        LiveGame game;
        game.attach();

        auto tank = game.spawn("TANK", game.local, SimVector(0_ss, 0_ss, 0_ss));
        game.sim.getUnitState(tank).orders.push_back(MoveOrder(SimVector(400_ss, 0_ss, 0_ss)));
        auto kbot = game.spawn("KBOT", game.local, SimVector(-60_ss, 0_ss, 0_ss));
        auto solar = game.sim.trySpawnUnit("SOLAR", game.local, SimVector(-60_ss, 0_ss, 60_ss), std::nullopt);
        REQUIRE(solar);
        game.sender->buildStarted(game.sim, kbot, *solar);

        auto batches = game.runAndTake(5);
        game.sim.getUnitState(*solar).finishBuilding(game.sim.unitDefinitions.at("SOLAR"));
        auto rest = game.runAndTake(395);
        batches.insert(batches.end(), rest.begin(), rest.end());
        REQUIRE(batches.size() == 400);

        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        defineLiveWorld(sim);
        auto peer = addPlayer(sim, "peer");

        TadPuppetDriver driver(sim, game.maxUnits, liveLoadOrder());
        driver.addPlayer(1, peer);

        for (const auto& batch : batches)
        {
            driver.onPacket(TadPacket{static_cast<uint16_t>(batch.tick), 1}, batch.subPackets);
        }

        auto playedTank = findUnitOfType(sim, "TANK");
        REQUIRE(playedTank);
        auto walked = sim.getUnitState(*playedTank).position != SimVector(0_ss, 0_ss, 0_ss);
        REQUIRE(walked);
        REQUIRE(findUnitOfType(sim, "SOLAR"));

        // The puppets follow by dead reckoning along the replicated path, so
        // what the owner's full-state record finds is a small correction rather
        // than a jump.
        REQUIRE(driver.stats().groundDrift.samples > 0);
        for (auto distance : driver.stats().groundDrift.distances)
        {
            REQUIRE(distance <= 32.0);
        }
    }
}
