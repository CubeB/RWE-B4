#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/io/tad/tad_headers.h>
#include <rwe/sim/DemoRecorder.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>
#include <rwe/util/rwe_string.h>
#include <sstream>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        struct RecordingHandler : TadHandler
        {
            std::optional<TadHeader> header;
            std::vector<TadPlayer> players;
            std::vector<TadPlayerStatus> statuses;
            std::vector<TadUnitTableEntry> unitTable;
            std::vector<TadPacket> packets;
            std::vector<std::vector<TadBytes>> subPackets;
            TadWalkStats stats;

            void onHeader(const TadHeader& h) override { header = h; }
            void onPlayer(const TadPlayer& p, unsigned int, unsigned int) override { players.push_back(p); }
            void onPlayerStatus(const TadPlayerStatus& s, unsigned int, unsigned int) override { statuses.push_back(s); }

            void onUnitData(const TadBytes& d) override
            {
                if (auto table = tadDecodeUnitTable(d))
                {
                    unitTable = table->restricted;
                }
            }

            void onPacket(const TadPacket& p, const std::vector<TadBytes>& subs, const TadWalkStats& s) override
            {
                packets.push_back(p);
                subPackets.push_back(subs);
                stats.unknownCodes += s.unknownCodes;
                stats.truncated += s.truncated;
                stats.failedDecompressions += s.failedDecompressions;
            }
        };

        RecordingHandler readDemo(const std::string& demo)
        {
            std::istringstream stream(demo);
            RecordingHandler handler;
            readTad(stream, handler);
            return handler;
        }

        std::optional<TadBytes> firstSubPacketOf(const std::vector<TadBytes>& subs, uint8_t code)
        {
            for (const auto& sub : subs)
            {
                if (!sub.empty() && sub[0] == code)
                {
                    return sub;
                }
            }
            return std::nullopt;
        }

        /** The index of the first subpacket with this code, or -1. */
        int subPacketPosition(const std::vector<TadBytes>& subs, uint8_t code)
        {
            for (std::size_t i = 0; i < subs.size(); ++i)
            {
                if (!subs[i].empty() && subs[i][0] == code)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        UnitDefinition makeGroundUnit(unsigned int buildTime)
        {
            UnitDefinition definition{};
            definition.buildTime = buildTime;
            definition.maxHitPoints = 100;
            definition.maxVelocity = 1_ss;
            definition.isMobile = true;
            return definition;
        }

        UnitDefinition makeBuilding(unsigned int buildTime)
        {
            UnitDefinition definition{};
            definition.buildTime = buildTime;
            definition.maxHitPoints = 100;
            return definition;
        }

        DemoRecorderSettings makeDemoSettings()
        {
            DemoRecorderSettings settings;
            settings.maxUnits = 4;
            settings.mapName = "Coast To Coast";
            // The order the loader would have captured, as if sorted by stem.
            settings.unitLoadOrder = {"ARMCOM", "ARMSOLAR", "ARMPEEP"};
            return settings;
        }

        TadUnitStateLayout makeDemoLayout()
        {
            // canFly in load order: the peeper flies, the others do not.
            return tadUnitStateLayout({false, false, true}, 4);
        }

        /** The index of the first packet carrying a code, or -1. */
        int packetWithCode(const RecordingHandler& handler, uint8_t code)
        {
            for (std::size_t i = 0; i < handler.subPackets.size(); ++i)
            {
                if (subPacketPosition(handler.subPackets[i], code) >= 0)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        /** The tick a packet's 0x2c carries, or nothing if it has none. */
        std::optional<uint32_t> tickOfPacket(const RecordingHandler& handler, std::size_t packetIndex, const TadUnitStateLayout& layout)
        {
            auto state = tadDecodeUnitState(*firstSubPacketOf(handler.subPackets[packetIndex], static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
            if (!state)
            {
                return std::nullopt;
            }
            return state->tick;
        }

        /** Attaches a recorder to a simulation; the simulation owns it from there. */
        void attachDemoRecorder(GameSimulation& simulation, std::ostringstream& stream, DemoRecorderSettings settings)
        {
            simulation.attachDemoRecorder(std::make_unique<DemoRecorder>(stream, simulation, std::move(settings)));
        }
    }

    TEST_CASE("DemoIdAllocator", "[demorecorder]")
    {
        SECTION("numbers ids by block arithmetic, one block per owner")
        {
            DemoIdAllocator ids(1000);
            PlayerId a(0);
            PlayerId b(1);

            REQUIRE(ids.allocate(a, UnitId(10)) == 1);
            REQUIRE(ids.allocate(a, UnitId(11)) == 2);
            REQUIRE(ids.allocate(b, UnitId(12)) == 1001);

            REQUIRE(ids.idOf(UnitId(10)) == 1);
            REQUIRE(ids.indexOf(UnitId(10)) == 0);
            REQUIRE(ids.indexOf(UnitId(11)) == 1);
            REQUIRE(ids.ownerOf(UnitId(12)) == b);
            REQUIRE(ids.usedBy(a) == 2);
            REQUIRE(ids.usedBy(b) == 1);
            REQUIRE_FALSE(ids.idOf(UnitId(99)));
            REQUIRE_FALSE(ids.ownerOf(UnitId(99)));
        }

        SECTION("recycles a released slot, lowest first")
        {
            DemoIdAllocator ids(1000);
            PlayerId a(0);

            REQUIRE(ids.allocate(a, UnitId(1)) == 1);
            REQUIRE(ids.allocate(a, UnitId(2)) == 2);
            REQUIRE(ids.allocate(a, UnitId(3)) == 3);

            ids.release(UnitId(2));
            ids.release(UnitId(1));
            REQUIRE(ids.usedBy(a) == 1);

            REQUIRE(ids.allocate(a, UnitId(4)) == 1);
            REQUIRE(ids.allocate(a, UnitId(5)) == 2);
            REQUIRE(ids.allocate(a, UnitId(6)) == 4);
        }

        SECTION("refuses once a player's block is full rather than wrapping")
        {
            DemoIdAllocator ids(2);
            PlayerId a(0);
            PlayerId b(1);

            REQUIRE(ids.allocate(a, UnitId(1)) == 1);
            REQUIRE(ids.allocate(a, UnitId(2)) == 2);
            REQUIRE_FALSE(ids.allocate(a, UnitId(3)));

            // The refusal is the owner's own block, not the allocator.
            REQUIRE(ids.allocate(b, UnitId(3)) == 3);

            ids.release(UnitId(1));
            REQUIRE(ids.allocate(a, UnitId(4)) == 1);
        }

        SECTION("hands a unit that already has an id the same one back")
        {
            DemoIdAllocator ids(1000);
            PlayerId a(0);

            REQUIRE(ids.allocate(a, UnitId(1)) == 1);
            REQUIRE(ids.allocate(a, UnitId(1)) == 1);
            REQUIRE(ids.usedBy(a) == 1);
        }

        SECTION("releasing a unit it has never seen is a no-op")
        {
            DemoIdAllocator ids(1000);
            ids.release(UnitId(1));
            REQUIRE(ids.usedBy(PlayerId(0)) == 0);
        }
    }

    TEST_CASE("DemoRecorder", "[demorecorder]")
    {
        GameSimulation simulation(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto arm = addWellStockedPlayer(simulation, "ARM");
        auto core = addWellStockedPlayer(simulation, "CORE");
        simulation.unitDefinitions["ARMCOM"] = makeGroundUnit(100);
        simulation.unitDefinitions["ARMSOLAR"] = makeBuilding(100);
        simulation.unitDefinitions["ARMPEEP"] = makeGroundUnit(100);
        simulation.unitDefinitions["ARMPEEP"].canFly = true;

        // One ground unit on the ARM side and one peeper on the CORE side,
        // so both senders have a block and the two serialisers are both used.
        auto armCommander = addUnitOfType(simulation, "ARMCOM", arm, SimVector(100_ss, 0_ss, 200_ss), makeEmptyCobScript());
        [[maybe_unused]] auto corePeeper = addUnitOfType(simulation, "ARMPEEP", core, SimVector(300_ss, 0_ss, 400_ss), makeEmptyCobScript());

        std::ostringstream stream;
        DemoRecorder recorder(stream, simulation, makeDemoSettings());

        auto layout = makeDemoLayout();

        SECTION("writes the container and one 0x2c per sender per tick")
        {
            simulation.gameTime += GameTime(4);
            recorder.endOfTick(simulation);
            recorder.close();

            auto handler = readDemo(stream.str());
            REQUIRE(handler.stats.total() == 0);
            REQUIRE(handler.header);
            REQUIRE(handler.header->version == 5);
            REQUIRE(handler.header->numPlayers == 2);
            REQUIRE(handler.header->maxUnits == 4);
            REQUIRE(handler.header->mapName == "Coast To Coast");

            REQUIRE(handler.players.size() == 2);
            REQUIRE(handler.players[0].name == "player");
            REQUIRE(handler.players[0].side == static_cast<int8_t>(TadSide::Arm));
            REQUIRE(handler.players[1].side == static_cast<int8_t>(TadSide::Core));

            // The status ids run from one so a 0x0c can name a player.
            REQUIRE(handler.statuses.size() == 2);
            REQUIRE(handler.statuses[0].checksum.matches());
            REQUIRE(handler.statuses[0].dplayId == 1);
            REQUIRE(handler.statuses[1].dplayId == 2);

            REQUIRE(handler.unitTable.size() == 3);

            // Two senders, one packet each, both at the tick the recorder
            // last saw, and each carrying its own unit state.
            REQUIRE(handler.packets.size() == 2);
            REQUIRE(handler.packets[0].sender == 0);
            REQUIRE(handler.packets[1].sender == 1);
            for (const auto& subs : handler.subPackets)
            {
                auto sub = firstSubPacketOf(subs, static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove));
                REQUIRE(sub);
                auto state = tadDecodeUnitState(*sub, layout);
                REQUIRE(state);
                REQUIRE(state->tick == 4);
                REQUIRE(state->sync);
            }

            // Tick 4 puts ARM's index 0 (the commander) up for its full state.
            auto armState = tadDecodeUnitState(*firstSubPacketOf(handler.subPackets[0], static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
            REQUIRE(armState->sync->index == 0);
            REQUIRE(armState->sync->typeIndex == 1);
            REQUIRE(armState->sync->health == 100);
            REQUIRE(armState->sync->buildProgress == 0);
            REQUIRE(armState->sync->speed);
            REQUIRE(armState->sync->position == TadPosition{simScalarToFixed(100_ss), 0, simScalarToFixed(200_ss)});

            // The peeper's block is index 0 too, being its owner's first unit.
            auto coreState = tadDecodeUnitState(*firstSubPacketOf(handler.subPackets[1], static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
            REQUIRE(coreState->sync->index == 0);
            REQUIRE(coreState->sync->typeIndex == 3);
        }

        SECTION("decodes a ground mover from an empty path and an air mover from its mode")
        {
            simulation.gameTime += GameTime(1);
            recorder.endOfTick(simulation);
            recorder.close();

            auto handler = readDemo(stream.str());

            // Tick 1 puts index 1 up for a full state on each side; the
            // updates carry one entry each because neither unit has been
            // sent before.
            auto armState = tadDecodeUnitState(*firstSubPacketOf(handler.subPackets[0], static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
            REQUIRE(armState);
            REQUIRE(armState->updates.size() == 1);
            REQUIRE(armState->updates[0].index == 0);
            REQUIRE(armState->updates[0].typeIndex == 1);
            auto* ground = std::get_if<TadGroundPath>(&armState->updates[0].mover);
            REQUIRE(ground);
            REQUIRE(ground->waypoints.empty());
            REQUIRE(armState->sync->index == 1);
            REQUIRE(armState->sync->typeIndex == 0);

            auto coreState = tadDecodeUnitState(*firstSubPacketOf(handler.subPackets[1], static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
            REQUIRE(coreState);
            REQUIRE(coreState->updates.size() == 1);
            auto* air = std::get_if<TadAirMover>(&coreState->updates[0].mover);
            REQUIRE(air);
            REQUIRE(std::holds_alternative<std::monostate>(air->goal));
            REQUIRE(air->movementMode == 1);
        }

        SECTION("sends 0x09 before the 0x2c, and 0x12 with the remembered builder")
        {
            auto frame = addUnitOfType(simulation, "ARMSOLAR", arm, SimVector(140_ss, 0_ss, 200_ss), makeEmptyCobScript());
            simulation.getUnitState(frame).buildTimeCompleted = 0;
            recorder.unitCreated(simulation, frame);

            simulation.gameTime += GameTime(1);
            recorder.buildStarted(simulation, armCommander, frame);
            recorder.endOfTick(simulation);

            {
                auto handler = readDemo(stream.str());
                REQUIRE(handler.packets.size() == 2);
                const auto& subs = handler.subPackets[0];

                auto buildStartedPos = subPacketPosition(subs, static_cast<uint8_t>(TadSubPacketCode::UnitBuildStarted));
                auto statePos = subPacketPosition(subs, static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove));
                REQUIRE(buildStartedPos >= 0);
                REQUIRE(statePos >= 0);
                REQUIRE(buildStartedPos < statePos);

                auto built = tadDecodeBuildStarted(subs[buildStartedPos]);
                REQUIRE(built);
                REQUIRE(built->typeIndex == 2);
                REQUIRE(built->unitId == 2);
                REQUIRE(built->position == TadPosition{simScalarToFixed(140_ss), 0, simScalarToFixed(200_ss)});

                // No 0x12 yet: the frame has only just been placed, and the
                // full-state record at tick 1 is the frame itself, showing
                // progress that is not complete.
                REQUIRE(subPacketPosition(subs, static_cast<uint8_t>(TadSubPacketCode::UnitBuildFinished)) < 0);
                auto state = tadDecodeUnitState(*firstSubPacketOf(subs, static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
                REQUIRE(state->sync->index == 1);
                REQUIRE(state->sync->typeIndex == 2);
                REQUIRE(state->sync->buildProgress > 0);
            }

            simulation.getUnitState(frame).buildTimeCompleted = simulation.unitDefinitions.at("ARMSOLAR").buildTime;
            simulation.gameTime += GameTime(1);
            recorder.endOfTick(simulation);
            recorder.close();

            {
                auto handler = readDemo(stream.str());
                // The first tick's packet and then the completing tick's, two
                // senders each.
                REQUIRE(handler.packets.size() == 4);
                auto finished = tadDecodeBuildFinished(*firstSubPacketOf(handler.subPackets[2], static_cast<uint8_t>(TadSubPacketCode::UnitBuildFinished)));
                REQUIRE(finished);
                REQUIRE(finished->unitId == 2);
                REQUIRE(finished->builderId == 1);
            }
        }

        SECTION("reuses a dead unit's slot in the demo's own blocks")
        {
            // RWE recycles UnitIds, so the recorder has to release a slot when
            // a unit leaves or a new unit inherits the old one's demo id.
            recorder.unitRemoved(armCommander);
            auto replacement = addUnitOfType(simulation, "ARMCOM", arm, SimVector(120_ss, 0_ss, 200_ss), makeEmptyCobScript());
            recorder.unitCreated(simulation, replacement);

            simulation.gameTime += GameTime(1);
            recorder.endOfTick(simulation);
            recorder.close();

            auto handler = readDemo(stream.str());
            auto state = tadDecodeUnitState(*firstSubPacketOf(handler.subPackets[0], static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
            REQUIRE(state);
            REQUIRE(state->updates.size() == 1);
            REQUIRE(state->updates[0].index == 0);
            REQUIRE(state->updates[0].typeIndex == 1);
        }
    }

    TEST_CASE("DemoRecorder records a shot as the shooter's owner sent it", "[demorecorder]")
    {
        // The scene is weaponfiretick.test.cpp's: a Sentinel at the origin with
        // a line-of-sight laser, and an extractor 152 units up the z axis, level
        // with the barrel so the round flies level. That test pins the tick the
        // damage lands on; this one pins what the 0x0d carries.
        GameSimulation sim(makeFlatTerrain(256, 256), 0u, 0, 0);
        sim.lineOfSightMode = LineOfSightMode::Circular;
        auto arm = addPlayer(sim, "arm");
        auto core = addPlayer(sim, "core");

        std::vector<UnitPieceDefinition> shooterPieces{
            UnitPieceDefinition{"base", SimVector(0_ss, 20_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["shootermodel"] = createUnitModelDefinition(40_ss, std::move(shooterPieces));
        std::vector<UnitPieceDefinition> victimPieces{
            UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["victimmodel"] = createUnitModelDefinition(20_ss, std::move(victimPieces));

        WeaponDefinition laser{};
        laser.physicsType = ProjectilePhysicsTypeLineOfSight();
        laser.maxRange = 600_ss;
        laser.reloadTime = SimScalar(0.2f);
        laser.burst = 1;
        laser.velocity = 32_ss;
        laser.damageRadius = 16_ss;
        laser.damage["DEFAULT"] = 300;
        laser.turret = true;
        laser.tolerance = SimAngle(1000);
        laser.pitchTolerance = SimAngle(1000);
        laser.energyPerShot = Energy(150.0f);
        sim.weaponDefinitions["LASER"] = laser;

        UnitDefinition shooter{};
        shooter.objectName = "shootermodel";
        shooter.isMobile = true;
        shooter.canMove = false;
        shooter.canAttack = true;
        shooter.sightDistance = 512u;
        shooter.maxHitPoints = 3075;
        shooter.shootMe = true;
        shooter.standingFireOrder = UnitFireOrders::FireAtWill;
        shooter.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
        shooter.weapon1 = "LASER";
        sim.unitDefinitions["SHOOTER"] = shooter;

        UnitDefinition victim{};
        victim.objectName = "victimmodel";
        victim.isMobile = true;
        victim.canMove = false;
        victim.sightDistance = 192u;
        victim.maxHitPoints = 1000000;
        victim.shootMe = true;
        victim.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["VICTIM"] = victim;

        auto script = makeEmptyCobScript({"base"});
        sim.unitScriptDefinitions["SHOOTER"] = *script;
        sim.unitScriptDefinitions["VICTIM"] = *script;

        auto shooterId = sim.trySpawnUnit("SHOOTER", arm, SimVector(0_ss, 0_ss, 0_ss), std::nullopt);
        REQUIRE(shooterId);
        auto victimId = sim.trySpawnUnit("VICTIM", core, SimVector(0_ss, 0_ss, 152_ss), std::nullopt);
        REQUIRE(victimId);
        // A freshly spawned unit is a nanoframe with no hit points; give both
        // the whole of what their definitions say.
        sim.getUnitState(*shooterId).hitPoints = sim.unitDefinitions.at("SHOOTER").maxHitPoints;
        sim.getUnitState(*victimId).hitPoints = sim.unitDefinitions.at("VICTIM").maxHitPoints;
        // Level flight: the barrel is twenty units up, so the victim stands there.
        sim.getUnitState(*victimId).position.y = 20_ss;
        sim.getUnitState(*victimId).previousPosition.y = 20_ss;

        std::ostringstream stream;
        DemoRecorderSettings settings;
        settings.maxUnits = 4;
        settings.mapName = "Coast To Coast";
        settings.unitLoadOrder = {"SHOOTER", "VICTIM"};
        attachDemoRecorder(sim, stream, settings);

        unsigned int fireTick = 0;
        for (unsigned int t = 1; t <= 600; ++t)
        {
            sim.events.clear();
            sim.tick();
            if (fireTick == 0)
            {
                for (const auto& e : sim.events)
                {
                    if (std::holds_alternative<FireWeaponEvent>(e))
                    {
                        fireTick = t;
                        break;
                    }
                }
            }
            // The round needs a few ticks to cross to the victim, and both
            // halves of the pair are wanted in the file.
            if (fireTick > 0 && sim.getUnitState(*victimId).hitPoints < 1000000)
            {
                break;
            }
        }
        REQUIRE(fireTick > 0);
        REQUIRE(sim.getUnitState(*victimId).hitPoints < 1000000);
        sim.demoRecorder->close();

        auto handler = readDemo(stream.str());
        REQUIRE(handler.stats.total() == 0);

        // The shooter's owner sends it, in the unit pass: the 0x0d sits before
        // its own tick's 0x2c, which is why the corpus reads a shot a tick early.
        auto shotPacket = packetWithCode(handler, static_cast<uint8_t>(TadSubPacketCode::WeaponFired));
        REQUIRE(shotPacket >= 0);
        REQUIRE(handler.packets[shotPacket].sender == 0);
        const auto& subs = handler.subPackets[shotPacket];
        REQUIRE(subPacketPosition(subs, static_cast<uint8_t>(TadSubPacketCode::WeaponFired))
            < subPacketPosition(subs, static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)));

        auto shot = tadDecodeShot(*firstSubPacketOf(subs, static_cast<uint8_t>(TadSubPacketCode::WeaponFired)));
        REQUIRE(shot);
        REQUIRE(shot->shooterId == 1);
        REQUIRE(shot->weaponSlot == 0);
        REQUIRE(shot->targetId == 5);
        REQUIRE(shot->origin == TadPosition{0, simScalarToFixed(20_ss), 0});
        REQUIRE(shot->target == TadPosition{0, simScalarToFixed(20_ss), simScalarToFixed(152_ss)});

        // The damage the same laser caused goes out after the 0x2c, from the
        // same sender, with the weapon's own damage figure on it.
        auto damagePacket = packetWithCode(handler, static_cast<uint8_t>(TadSubPacketCode::UnitTakeDamage));
        REQUIRE(damagePacket >= 0);
        REQUIRE(handler.packets[damagePacket].sender == 0);
        REQUIRE(subPacketPosition(handler.subPackets[damagePacket], static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove))
            < subPacketPosition(handler.subPackets[damagePacket], static_cast<uint8_t>(TadSubPacketCode::UnitTakeDamage)));
        auto damage = tadDecodeDamage(*firstSubPacketOf(handler.subPackets[damagePacket], static_cast<uint8_t>(TadSubPacketCode::UnitTakeDamage)));
        REQUIRE(damage);
        REQUIRE(damage->victimId == 5);
        REQUIRE(damage->attackerId == 1);
        REQUIRE(damage->damage == 300);
    }

    TEST_CASE("DemoRecorder records damage from the attacker's owner and death from the victim's", "[demorecorder]")
    {
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto arm = addPlayer(sim, "arm");
        auto core = addPlayer(sim, "core");
        std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(20_ss, std::move(pieces));
        sim.unitDefinitions["ARMCOM"] = makeGroundUnit(0);
        sim.unitDefinitions["ARMCOM"].objectName = "model";
        sim.unitDefinitions["CORCOM"] = makeGroundUnit(0);
        sim.unitDefinitions["CORCOM"].objectName = "model";

        auto script = makeEmptyCobScript();
        auto attacker = addUnitOfType(sim, "ARMCOM", arm, SimVector(100_ss, 0_ss, 200_ss), script);
        auto victim = addUnitOfType(sim, "CORCOM", core, SimVector(120_ss, 0_ss, 200_ss), script);

        std::ostringstream stream;
        DemoRecorderSettings settings;
        settings.maxUnits = 4;
        settings.mapName = "Coast To Coast";
        settings.unitLoadOrder = {"ARMCOM", "CORCOM"};
        attachDemoRecorder(sim, stream, settings);

        SECTION("a killing weapon hit carries its severity and leaves a corpse")
        {
            // 250 against 100 hit points is 150 overkill: clamp(100*150/100/2) = 75.
            sim.applyDamage(victim, 250, attacker);
            sim.demoRecorder->endOfTick(sim);
            sim.demoRecorder->close();

            auto handler = readDemo(stream.str());

            auto damage = tadDecodeDamage(*firstSubPacketOf(handler.subPackets[0], static_cast<uint8_t>(TadSubPacketCode::UnitTakeDamage)));
            REQUIRE(damage);
            REQUIRE(damage->victimId == 5);
            REQUIRE(damage->attackerId == 1);
            REQUIRE(damage->damage == 250);
            // The victim's owner never sends the damage record.
            REQUIRE(packetWithCode(handler, static_cast<uint8_t>(TadSubPacketCode::UnitTakeDamage)) == 0);

            // The death is the victim's owner's, with the killer named by both
            // its unit id and its DirectPlay id, and cause 1 in the high nibble.
            auto death = tadDecodeDeath(*firstSubPacketOf(handler.subPackets[1], static_cast<uint8_t>(TadSubPacketCode::UnitKilled)));
            REQUIRE(death);
            REQUIRE(death->unitId == 5);
            REQUIRE(death->killerId == 1);
            REQUIRE(death->killerDplayId == 1);
            REQUIRE(death->severity == 75);
            REQUIRE(death->cause() == 1);
            REQUIRE(death->corpseLevel() == 1);
        }

        SECTION("a hit from nothing is the victim's own record and names no killer")
        {
            sim.applyDamage(victim, 250);
            sim.demoRecorder->endOfTick(sim);
            sim.demoRecorder->close();

            auto handler = readDemo(stream.str());
            REQUIRE(handler.packets.size() == 2);

            auto damage = tadDecodeDamage(*firstSubPacketOf(handler.subPackets[1], static_cast<uint8_t>(TadSubPacketCode::UnitTakeDamage)));
            REQUIRE(damage);
            REQUIRE(damage->victimId == 5);
            REQUIRE(damage->attackerId == 0);

            auto death = tadDecodeDeath(*firstSubPacketOf(handler.subPackets[1], static_cast<uint8_t>(TadSubPacketCode::UnitKilled)));
            REQUIRE(death);
            REQUIRE(death->killerId == 0);
            REQUIRE(death->killerDplayId == 0xffffffffu);
        }

        SECTION("a dying unit's blast is sent by the peer whose unit it was")
        {
            // RWE's only no-attacker damage is an explodeAs, and the projectile
            // carries the dying unit's owner. The one real recording this could
            // be checked in sends all 89 of its no-attacker records from the
            // peer that is not the victim's, so that owner is the sender.
            //
            // A scene of its own, because a blast walks the occupied grid and
            // the hand-placed units above were never stamped onto it.
            GameSimulation blastSim(makeFlatTerrain(32, 32), 0u, 0, 0);
            auto blastArm = addPlayer(blastSim, "arm");
            auto blastCore = addPlayer(blastSim, "core");
            std::vector<UnitPieceDefinition> blastPieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            blastSim.unitModelDefinitions["model"] = createUnitModelDefinition(20_ss, std::move(blastPieces));
            blastSim.unitDefinitions["ARMCOM"] = makeGroundUnit(0);
            blastSim.unitDefinitions["ARMCOM"].objectName = "model";
            blastSim.unitDefinitions["ARMCOM"].explodeAs = "BLAST";
            blastSim.unitDefinitions["ARMCOM"].movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            blastSim.unitDefinitions["CORCOM"] = makeGroundUnit(0);
            blastSim.unitDefinitions["CORCOM"].objectName = "model";
            blastSim.unitDefinitions["CORCOM"].movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};

            WeaponDefinition blast{};
            blast.physicsType = ProjectilePhysicsTypeLineOfSight();
            blast.maxRange = 32_ss;
            blast.velocity = 1_ss;
            blast.damageRadius = 64_ss;
            blast.damage["DEFAULT"] = 50;
            blastSim.weaponDefinitions["BLAST"] = blast;

            auto blastScript = makeEmptyCobScript({"base"});
            blastSim.unitScriptDefinitions["ARMCOM"] = *blastScript;
            blastSim.unitScriptDefinitions["CORCOM"] = *blastScript;

            auto bomber = blastSim.trySpawnUnit("ARMCOM", blastArm, SimVector(100_ss, 0_ss, 200_ss), std::nullopt);
            REQUIRE(bomber);
            auto caught = blastSim.trySpawnUnit("CORCOM", blastCore, SimVector(120_ss, 0_ss, 200_ss), std::nullopt);
            REQUIRE(caught);

            std::ostringstream blastStream;
            DemoRecorderSettings blastSettings;
            blastSettings.maxUnits = 4;
            blastSettings.mapName = "Coast To Coast";
            blastSettings.unitLoadOrder = {"ARMCOM", "CORCOM"};
            attachDemoRecorder(blastSim, blastStream, blastSettings);

            blastSim.killUnit(*bomber);
            blastSim.demoRecorder->endOfTick(blastSim);
            blastSim.demoRecorder->close();

            auto handler = readDemo(blastStream.str());

            // The blast caught the enemy twenty units away, and the record is
            // the ARM sender's even though it names no attacker.
            auto damage = tadDecodeDamage(*firstSubPacketOf(handler.subPackets[0], static_cast<uint8_t>(TadSubPacketCode::UnitTakeDamage)));
            REQUIRE(damage);
            REQUIRE(damage->victimId == 5);
            REQUIRE(damage->attackerId == 0);
            REQUIRE(damage->damage > 0);
            REQUIRE(packetWithCode(handler, static_cast<uint8_t>(TadSubPacketCode::UnitTakeDamage)) == 0);

            // And the dying unit's own death is on that side too.
            auto death = tadDecodeDeath(*firstSubPacketOf(handler.subPackets[0], static_cast<uint8_t>(TadSubPacketCode::UnitKilled)));
            REQUIRE(death);
            REQUIRE(death->unitId == 1);
        }

        SECTION("reclaim is cause 5 and leaves nothing")
        {
            REQUIRE(sim.reclaimUnit(victim, arm, 1));
            sim.demoRecorder->endOfTick(sim);
            sim.demoRecorder->close();

            auto handler = readDemo(stream.str());
            auto death = tadDecodeDeath(*firstSubPacketOf(handler.subPackets[1], static_cast<uint8_t>(TadSubPacketCode::UnitKilled)));
            REQUIRE(death);
            REQUIRE(death->unitId == 5);
            REQUIRE(death->severity == 0);
            REQUIRE(death->cause() == 5);
            REQUIRE(death->corpseLevel() == 0);
        }

        SECTION("self-destruct is cause 3 and leaves nothing")
        {
            sim.selfDestructUnit(victim);
            sim.demoRecorder->endOfTick(sim);
            sim.demoRecorder->close();

            auto handler = readDemo(stream.str());
            auto death = tadDecodeDeath(*firstSubPacketOf(handler.subPackets[1], static_cast<uint8_t>(TadSubPacketCode::UnitKilled)));
            REQUIRE(death);
            REQUIRE(death->cause() == 3);
            REQUIRE(death->severity == 0);
            REQUIRE(death->corpseLevel() == 0);
        }

        SECTION("an unfinished unit removed is cause 9 and leaves nothing")
        {
            sim.removeUnfinishedUnit(victim);
            sim.demoRecorder->endOfTick(sim);
            sim.demoRecorder->close();

            auto handler = readDemo(stream.str());
            auto death = tadDecodeDeath(*firstSubPacketOf(handler.subPackets[1], static_cast<uint8_t>(TadSubPacketCode::UnitKilled)));
            REQUIRE(death);
            REQUIRE(death->cause() == 9);
            REQUIRE(death->severity == 0);
            REQUIRE(death->corpseLevel() == 0);
        }
    }

    TEST_CASE("DemoRecorder destroys and recreates a captured unit in its captor's block", "[demorecorder]")
    {
        // D10's answer is the original's own: 0x488570 kills the old record
        // with cause 4 and creates a new one under the captor, so a demo never
        // has to express "the same id changed hands".
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto captor = addPlayer(sim, "captor");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["SOLAR"] = makeBuilding(0);
        sim.unitDefinitions["PEEPER"] = makeBuilding(0);

        auto script = makeEmptyCobScript();
        [[maybe_unused]] auto first = addUnitOfType(sim, "SOLAR", enemy, SimVector(100_ss, 0_ss, 100_ss), script);
        auto second = addUnitOfType(sim, "SOLAR", enemy, SimVector(200_ss, 0_ss, 200_ss), script);

        std::ostringstream stream;
        DemoRecorderSettings settings;
        settings.maxUnits = 4;
        settings.mapName = "Coast To Coast";
        settings.unitLoadOrder = {"SOLAR", "PEEPER"};
        attachDemoRecorder(sim, stream, settings);

        REQUIRE(sim.captureUnit(second, captor));

        // The next enemy unit reuses the slot the capture freed, which is what
        // makes the demo's id arithmetic survive an ownership change.
        auto replacement = addUnitOfType(sim, "PEEPER", enemy, SimVector(300_ss, 0_ss, 300_ss), script);
        sim.demoRecorder->unitCreated(sim, replacement);

        sim.demoRecorder->endOfTick(sim);
        sim.demoRecorder->close();

        auto handler = readDemo(stream.str());
        auto layout = tadUnitStateLayout({false, false}, 4);

        // The old owner's sender carries the cause-4 death for the old id.
        auto death = tadDecodeDeath(*firstSubPacketOf(handler.subPackets[1], static_cast<uint8_t>(TadSubPacketCode::UnitKilled)));
        REQUIRE(death);
        REQUIRE(death->unitId == 2);
        REQUIRE(death->killerId == 0);
        REQUIRE(death->killerDplayId == 0xffffffffu);
        REQUIRE(death->severity == 0);
        REQUIRE(death->cause() == 4);
        REQUIRE(death->corpseLevel() == 0);

        // The captor's block carries the unit under a fresh id: its first
        // index, with the solar's type.
        auto captorState = tadDecodeUnitState(*firstSubPacketOf(handler.subPackets[0], static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
        REQUIRE(captorState);
        REQUIRE(captorState->updates.size() == 1);
        REQUIRE(captorState->updates[0].index == 0);
        REQUIRE(captorState->updates[0].typeIndex == 1);

        // The enemy's block has the first solar and the replacement sitting in
        // the freed index 1, which is the slot recycling.
        auto enemyState = tadDecodeUnitState(*firstSubPacketOf(handler.subPackets[1], static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove)), layout);
        REQUIRE(enemyState);
        REQUIRE(enemyState->updates.size() == 2);
        REQUIRE(enemyState->updates[0].index == 0);
        REQUIRE(enemyState->updates[0].typeIndex == 1);
        REQUIRE(enemyState->updates[1].index == 1);
        REQUIRE(enemyState->updates[1].typeIndex == 2);
    }

    TEST_CASE("DemoRecorder samples resources on the corpus cadence and the speed once", "[demorecorder]")
    {
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto arm = addPlayer(sim, "arm");
        auto core = addPlayer(sim, "core");

        sim.getPlayer(arm).metal = Metal(777.5f);
        sim.getPlayer(arm).energy = Energy(432.25f);
        sim.getPlayer(arm).maxMetal = Metal(1500.0f);
        sim.getPlayer(arm).maxEnergy = Energy(2000.0f);
        sim.getPlayer(arm).metalProduced = Metal(1000.0f);
        sim.getPlayer(arm).energyProduced = Energy(5000.0f);
        sim.getPlayer(arm).metalExcess = Metal(100.0f);
        sim.getPlayer(arm).energyExcess = Energy(500.0f);

        sim.getPlayer(core).metal = Metal(11.5f);
        sim.getPlayer(core).energy = Energy(22.25f);
        sim.getPlayer(core).maxMetal = Metal(1000.0f);
        sim.getPlayer(core).maxEnergy = Energy(1000.0f);
        sim.getPlayer(core).metalProduced = Metal(3.0f);
        sim.getPlayer(core).energyProduced = Energy(4.0f);
        sim.getPlayer(core).metalExcess = Metal(1.0f);
        sim.getPlayer(core).energyExcess = Energy(2.0f);

        std::ostringstream stream;
        DemoRecorderSettings settings;
        settings.maxUnits = 4;
        settings.mapName = "Coast To Coast";
        settings.unitLoadOrder = {"ARMCOM"};
        attachDemoRecorder(sim, stream, settings);

        // A sample lands on a settle -- a multiple of thirty -- and every 120
        // ticks, which is the corpus's interval.
        sim.gameTime = GameTime(119);
        sim.demoRecorder->endOfTick(sim);
        sim.gameTime = GameTime(120);
        sim.demoRecorder->endOfTick(sim);
        sim.gameTime = GameTime(121);
        sim.demoRecorder->endOfTick(sim);
        sim.gameTime = GameTime(240);
        sim.demoRecorder->endOfTick(sim);
        sim.demoRecorder->close();

        auto handler = readDemo(stream.str());
        REQUIRE(handler.packets.size() == 8);

        // Packet 0 is tick 119: the game-start 0x19 goes out before the 0x2c,
        // and no 0x28 yet.
        REQUIRE(handler.subPackets[0][0][0] == static_cast<uint8_t>(TadSubPacketCode::Speed));
        auto speed = tadDecodeSpeed(handler.subPackets[0][0]);
        REQUIRE(speed);
        REQUIRE(speed->value == 256);
        REQUIRE(handler.subPackets[0][1][0] == static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove));
        REQUIRE(packetWithCode(handler, static_cast<uint8_t>(TadSubPacketCode::PlayerResourceInfo)) >= 2);

        // Tick 120: both senders, each its own state, the four settled slots
        // exactly and the counters derived from produced and excess.
        auto armStats = tadDecodeResourceStats(*firstSubPacketOf(handler.subPackets[2], static_cast<uint8_t>(TadSubPacketCode::PlayerResourceInfo)));
        REQUIRE(armStats);
        REQUIRE(armStats->metalStored == 777.5f);
        REQUIRE(armStats->energyStored == 432.25f);
        REQUIRE(armStats->metalStorage == 1500.0f);
        REQUIRE(armStats->energyStorage == 2000.0f);
        REQUIRE(armStats->energyCounters[0] == 5000.0f);
        REQUIRE(armStats->energyCounters[1] == 500.0f);
        REQUIRE(armStats->energyCounters[2] == 4500.0f);
        REQUIRE(armStats->metalCounters[0] == 1000.0f);
        REQUIRE(armStats->metalCounters[1] == 100.0f);
        REQUIRE(armStats->metalCounters[2] == 900.0f);

        auto coreStats = tadDecodeResourceStats(*firstSubPacketOf(handler.subPackets[3], static_cast<uint8_t>(TadSubPacketCode::PlayerResourceInfo)));
        REQUIRE(coreStats);
        REQUIRE(coreStats->metalStored == 11.5f);
        REQUIRE(coreStats->energyStored == 22.25f);
        REQUIRE(coreStats->energyCounters[0] == 4.0f);
        REQUIRE(coreStats->metalCounters[2] == 2.0f);

        // Tick 121 carries none, tick 240 carries the next sample, and the
        // 0x19 did not repeat.
        REQUIRE(subPacketPosition(handler.subPackets[4], static_cast<uint8_t>(TadSubPacketCode::PlayerResourceInfo)) < 0);
        REQUIRE(subPacketPosition(handler.subPackets[6], static_cast<uint8_t>(TadSubPacketCode::PlayerResourceInfo)) >= 0);
        REQUIRE(subPacketPosition(handler.subPackets[2], static_cast<uint8_t>(TadSubPacketCode::Speed)) < 0);
    }

    TEST_CASE("a factory's 0x09 and 0x12 are stamped on the ticks the simulation built on", "[demorecorder]")
    {
        // M3's arena run read a consistent +2 against the build-timing model,
        // and this is where the two ticks go: the frame is created at the end
        // of the tick its 0x09 is stamped with, the first lathe is two ticks
        // later (the factory's StartBuilding tick plus the tick after it), and
        // the 0x12 lands on the tick the last lathe completes it. The model's
        // `increments - 1` assumes the first increment lands on the 0x09's own
        // tick, which is the original's convention -- section 88's recorded
        // divergence -- so a factory's demo duration is the model plus two.
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addWellStockedPlayer(sim, "ARM");

        std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

        UnitDefinition commander{};
        commander.objectName = "model";
        commander.commander = true;
        commander.maxHitPoints = 100;
        commander.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
        sim.unitDefinitions["COM"] = commander;

        UnitDefinition factory{};
        factory.objectName = "model";
        factory.maxHitPoints = 100;
        factory.workerTimePerTick = 8;
        factory.buildDistance = 60_ss;
        factory.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
        factory.yardMap = Grid<YardMapCell>(4, 4, YardMapCell::Ground);
        sim.unitDefinitions["FACTORY"] = factory;

        UnitDefinition product{};
        product.objectName = "model";
        product.maxHitPoints = 100;
        // 2002 is not a multiple of 8, so RWE's integer accumulator and the
        // original's float32 fraction need the same 251 increments and
        // section 88's other tick is not in the way.
        product.buildTime = 2002;
        product.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
        product.yardMap = Grid<YardMapCell>(2, 2, YardMapCell::Ground);
        sim.unitDefinitions["PRODUCT"] = product;

        auto script = makeEmptyCobScript({"base"});
        sim.unitScriptDefinitions["COM"] = *script;
        sim.unitScriptDefinitions["FACTORY"] = *script;
        sim.unitScriptDefinitions["PRODUCT"] = *script;

        addUnitOfType(sim, "COM", player, SimVector(50_ss, 0_ss, 50_ss), script);
        auto factoryId = addUnitOfType(sim, "FACTORY", player, SimVector(200_ss, 0_ss, 200_ss), script);
        sim.getUnitState(factoryId).buildQueue.emplace_back("PRODUCT", 1);
        sim.getUnitState(factoryId).inBuildStance = true;

        std::ostringstream stream;
        DemoRecorderSettings settings;
        settings.maxUnits = 1000;
        settings.mapName = "Coast To Coast";
        settings.unitLoadOrder = {"COM", "FACTORY", "PRODUCT"};
        attachDemoRecorder(sim, stream, settings);

        std::optional<UnitId> frameId;
        int startTick = -1;
        int firstIncrementTick = -1;
        int finishTick = -1;
        unsigned int increments = 0;
        unsigned int lastProgress = 0;
        for (int t = 1; t <= 4000; ++t)
        {
            sim.tick();

            if (!frameId)
            {
                for (const auto& entry : sim.units)
                {
                    if (entry.second.unitType == "PRODUCT")
                    {
                        frameId = UnitId(entry.first);
                        startTick = t;
                        break;
                    }
                }
            }
            if (!frameId)
            {
                continue;
            }

            auto& frame = sim.getUnitState(*frameId);
            if (frame.buildTimeCompleted > lastProgress)
            {
                ++increments;
                lastProgress = frame.buildTimeCompleted;
                if (firstIncrementTick < 0)
                {
                    firstIncrementTick = t;
                }
            }
            if (!frame.isBeingBuilt(sim.unitDefinitions.at("PRODUCT")))
            {
                finishTick = t;
                break;
            }
        }
        REQUIRE(frameId);
        REQUIRE(firstIncrementTick > 0);
        REQUIRE(finishTick > 0);
        sim.demoRecorder->close();

        auto handler = readDemo(stream.str());
        auto layout = tadUnitStateLayout({false, false, false}, 1000);

        auto startedPacket = packetWithCode(handler, static_cast<uint8_t>(TadSubPacketCode::UnitBuildStarted));
        auto finishedPacket = packetWithCode(handler, static_cast<uint8_t>(TadSubPacketCode::UnitBuildFinished));
        REQUIRE(startedPacket >= 0);
        REQUIRE(finishedPacket >= 0);
        REQUIRE(tickOfPacket(handler, startedPacket, layout) == static_cast<uint32_t>(startTick));
        REQUIRE(tickOfPacket(handler, finishedPacket, layout) == static_cast<uint32_t>(finishTick));

        // The decomposition, tick for tick: first lathe two ticks after the
        // frame appears, the last one completing it, and the model's duration
        // (`increments - 1`) short by exactly those two.
        REQUIRE(firstIncrementTick == startTick + 2);
        REQUIRE(finishTick == firstIncrementTick + static_cast<int>(increments) - 1);
        REQUIRE(finishTick - startTick == static_cast<int>(increments) + 1);
    }
}
