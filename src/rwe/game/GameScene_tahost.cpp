#include "GameScene.h"
#include <algorithm>
#include <cstdlib>
#include <imgui.h>
#include <optional>
#include <rwe/game/PlayerCommand.h>
#include <rwe/net/ta/TaHostGame.h>
#include <rwe/net/ta/TaOutboundBatcher.h>
#include <rwe/puppet/TaLiveReceiver.h>
#include <rwe/puppet/TadPuppetDriver.h>
#include <rwe/sim/MixedOwnership.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/TaLiveBatch.h>
#include <rwe/sim/TaLiveSender.h>
#include <rwe/sim/TaPeerIds.h>
#include <rwe/util/SimpleLogger.h>
#include <string>
#include <unordered_map>
#include <vector>

// Playing against a peer that runs its own simulation: RWE owns the Local
// player's units and puppets the rest, which is the mixed-ownership mode
// docs/TA-NETWORK.md is about. Split out of GameScene.cpp for the reason at
// the head of GameScene_render.cpp.
//
// The scene owns the tick loop and the simulation, and the peer is on a thread
// of its own (TaHostGame), so everything crossing between them is a value
// taken from or put into that object's two queues. Nothing in this file
// touches a socket, and nothing on the other thread touches the simulation.

namespace rwe
{
    namespace
    {
        /** Ticks to a UDP message, as a real TA sends them: six, thirty a second. */
        constexpr unsigned int TicksPerPacket = 6;

        /** How often the log says where the peer has got to. */
        constexpr unsigned int ReportEveryTicks = 150;

        /** How near a unit of ours has to be to one of the peer's to be ordered at it by the test aid. */
        constexpr SimScalar TaHostAttackRange = 400_ss;

        /** What RWE_TA_HOST_ATTACK puts beside the peer, a gun that reaches further than a commander. */
        constexpr const char* TaHostAttackUnit = "ARMPW";
    }

    struct GameScene::TaHostLink
    {
        std::shared_ptr<TaHostGame> host;

        /** The joiner: Remote, and the only player this driver puppets. */
        PlayerId remote;

        /** Its DirectPlay id, which is what a message to it is addressed by. */
        std::uint32_t peer{0};

        std::unique_ptr<TadPuppetDriver> driver;
        std::unique_ptr<TaLiveReceiver> receiver;
        std::unique_ptr<TaLiveSender> sender;
        std::unique_ptr<TaOutboundBatcher> traffic;

        /** Subpackets queued for the next message, and how many ticks they are. */
        std::vector<TadBytes> outgoing;
        unsigned int outgoingTicks{0};

        /**
         * This machine's own wire id for each of its units, rebuilt only when
         * an incoming `0x0b` names one this map does not have. The sender owns
         * the ids; this is the other direction of its lookup, and a walk of the
         * unit list per hit under fire is not worth it.
         */
        std::unordered_map<std::uint16_t, UnitId> localByWireId;

        /** Hits taken from the peer, for the panel and the log. */
        unsigned int damageTaken{0};

        /** Hits RWE has put on the peer, which are the 0x0b records it sends. */
        unsigned int damageDealt{0};
        bool peerPaused{false};

        /**
         * The scene tick the driver counts from.
         *
         * The driver numbers ticks from the first packet it is handed, which
         * is the tick the receiver anchored the peer's stream to, and this
         * game's own tick at that moment is the one the two numberings have to
         * be read against: the map takes some seconds to load, so by the time
         * the peer's first packet lands the scene tick is already hundreds and
         * the driver's first.
         */
        std::optional<unsigned int> driverOrigin;
        unsigned int appliedPackets{0};

        /** The tick the test aid last ordered attacks on. */
        unsigned int lastAttackOrderTick{0};

        /** Whether RWE_TA_HOST_ATTACK has put its one unit beside the peer yet. */
        bool attackSpawned{false};

        /** Attack orders the aid has issued, which is how many units it has in range. */
        unsigned int attackOrders{0};
    };

    void GameScene::enableTaHost(PlayerId remotePlayer, const std::vector<std::string>& unitLoadOrder)
    {
        if (!gameParameters.taHostGame)
        {
            throw std::runtime_error("enableTaHost: the game has no TA host attached");
        }

        auto link = std::make_shared<TaHostLink>();
        link->host = gameParameters.taHostGame;
        link->remote = remotePlayer;

        auto lobby = link->host->lobbyState();
        if (!lobby.launched)
        {
            throw std::runtime_error("enableTaHost: the game was loaded before the joiner was in it");
        }
        link->peer = lobby.launched->address.playerId;

        const auto& config = link->host->config();

        link->driver = std::make_unique<TadPuppetDriver>(simulation, config.maxUnits, unitLoadOrder);
        link->driver->setExternalClock(true);
        // The joiner is the session's second player, which is the sender byte
        // the driver is keyed on; its own 0x09 says which id block that is.
        link->driver->addPlayer(1, remotePlayer);
        link->receiver = std::make_unique<TaLiveReceiver>(*link->driver);

        auto peerId = link->peer;
        TaPeerIds peerIds;
        // Through the scene's own member rather than the local pointer: the
        // local one is moved into it below, and a callback that captured it
        // would be reading a stack slot that no longer exists.
        peerIds.unitId = [this](UnitId unit) { return taHostLink->driver->wireIdOf(unit); };
        peerIds.dplayId = [peerId](PlayerId player) -> std::optional<std::uint32_t> { return peerId; };

        TaLiveSenderSettings senderSettings;
        senderSettings.sender = localPlayerId;
        senderSettings.maxUnits = config.maxUnits;
        senderSettings.firstBlock = TaHostGame::hostUnitBlock;
        senderSettings.unitLoadOrder = unitLoadOrder;
        link->sender = std::make_unique<TaLiveSender>(simulation, std::move(senderSettings), std::move(peerIds));
        simulation.setTaLiveSender(link->sender->hooks());

        link->traffic = std::make_unique<TaOutboundBatcher>(
            [this](TaHostSession::PeerId id, std::span<const std::uint8_t> bytes, TaTransport transport) {
                taHostLink->host->queueOutbound(id, bytes, transport);
            },
            TadPacketCompressed);

        // A 0x0b naming a unit of ours is the attacker's to report, and the
        // driver does not puppet it: the wire id is turned back into the unit
        // it is and applied through the ordinary damage path, so the local
        // Killed script picks the death and the 0x0c goes out.
        link->driver->setIncomingDamageHandler(
            [this](std::uint16_t victimId, std::optional<UnitId> attacker, unsigned int damage) {
                auto& link = *taHostLink;
                auto it = link.localByWireId.find(victimId);
                if (it == link.localByWireId.end())
                {
                    link.localByWireId.clear();
                    for (const auto& [unit, state] : simulation.units)
                    {
                        if (auto id = link.sender->wireIdOf(UnitId(unit)))
                        {
                            link.localByWireId.emplace(*id, UnitId(unit));
                        }
                    }
                    it = link.localByWireId.find(victimId);
                    if (it == link.localByWireId.end())
                    {
                        // Not one of ours: it is the peer's own unit, which the
                        // driver has already dealt with, or a unit that has
                        // gone since the last rebuild.
                        return;
                    }
                }

                auto victim = it->second;
                if (!simulation.tryGetUnitState(victim) || !simulation.getUnitState(victim).isOwnedBy(localPlayerId))
                {
                    return;
                }

                ++link.damageTaken;
                applyIncomingDamage(simulation, victim, attacker, damage);
                LOG_INFO << "TA host: our unit " << victim.value << " took " << damage << " from the peer";
            });

        LOG_INFO << "TA host: '" << config.gameName << "' on " << config.mapName << ", the joiner is player "
                 << remotePlayer.value << " on peer 0x" << std::hex << link->peer << std::dec << ", "
                 << config.maxUnits << " units a block, ours in block " << TaHostGame::hostUnitBlock;

        taHostLink = std::move(link);
    }

    void GameScene::applyTaHostTick()
    {
        if (!taHostLink)
        {
            return;
        }
        auto& link = *taHostLink;

        for (auto& item : link.host->takeInbound())
        {
            if (item.kind == TaHostInbound::Kind::PeerLeft)
            {
                LOG_INFO << "TA host: the joiner left";
                removeRemotePlayer(simulation, link.remote);
                continue;
            }

            // The driver is keyed on the sender and the receiver on that
            // sender's own marker, which is the only thing that puts a peer's
            // records back in the order it sent them.
            TadPacket packet;
            packet.sender = item.sender;
            link.receiver->onPacket(packet, item.subPackets, sceneTime.value, item.sequence);
        }

        link.receiver->onTick(sceneTime.value);

        // The driver counts from its first packet, so the scene tick it is
        // given has to be read against the tick that packet arrived on; the
        // receiver's applied count is what says one has.
        if (link.receiver->stats().packetsApplied != link.appliedPackets)
        {
            link.appliedPackets = static_cast<unsigned int>(link.receiver->stats().packetsApplied);
            if (!link.driverOrigin)
            {
                link.driverOrigin = sceneTime.value;
                LOG_INFO << "TA host: the peer's stream is anchored at tick " << *link.driverOrigin;
            }
        }
        link.driver->applyTick(link.driverOrigin ? sceneTime.value - *link.driverOrigin : 0u);

        for (auto& line : link.driver->takeChat())
        {
            printChatLine(line.player, line.ally ? "(ally) " + line.text : line.text);
        }

        if (auto change = link.driver->takeSpeedChange())
        {
            // 256 is what every sender puts on the wire at game start and the
            // corpus calls it normal speed, so the two bytes are read here as
            // 256ths of normal: 0 is the pause TOTALA-EXE-INTERFACE.md 70
            // broadcasts, and 256 is 1x. That puts normal at level 10, which is
            // where the original's own 1..20 numbering has it, so the
            // game-start record leaves RWE's clock exactly as the player has it.
            if (*change == 0)
            {
                link.peerPaused = true;
                setPeerPaused(true);
            }
            else
            {
                setPeerGameSpeedLevel(std::clamp(static_cast<int>((*change * 10 + 128) / 256), 1, 20));
                if (link.peerPaused)
                {
                    link.peerPaused = false;
                    setPeerPaused(false);
                }
            }
        }

        // The two machines run their own clocks, so a batch goes out as it is
        // taken and six of them make the message a real TA sends.
        auto batch = link.sender->takeBatch();
        if (batch)
        {
            link.outgoing.insert(link.outgoing.end(), batch->subPackets.begin(), batch->subPackets.end());
            ++link.outgoingTicks;
        }

        if (!link.outgoing.empty() && (link.outgoingTicks >= TicksPerPacket || !batch))
        {
            auto peer = link.peer;
            for (auto& subPacket : link.outgoing)
            {
                link.traffic->queue(peer, std::move(subPacket), TaTransport::Udp);
            }
            link.outgoing.clear();
            link.outgoingTicks = 0;
            link.traffic->flush();
        }

        orderTaHostAttack();

        if (sceneTime.value % ReportEveryTicks == 0)
        {
            logTaHostStatus();
        }
    }

    void GameScene::orderTaHostAttack()
    {
        // Testing aid: RWE_TA_HOST_ATTACK=1 makes RWE hit the peer's units, so
        // that RWE's own 0x0b can be seen reaching a peer with nobody at a
        // keyboard. Like RWE_DEBUG_SPAWN, it is in the environment rather than
        // on the command line, and it does two things and nothing else:
        //
        // - once, when it first finds a peer unit, it puts one Peewee of ours
        //   beside it. A TA host and a TA joiner are usually a whole map apart
        //   -- Canal Crossing's two start positions are nine thousand units --
        //   and nothing walks that in the sixty seconds a check runs for, so an
        //   attack order alone would never reach anything and the check would
        //   pass for the wrong reason. The spawned unit is a real unit of the
        //   Local player, so the sender describes it and the peer sees it, and
        //   the hits it makes are the ordinary path.
        // - once a second after that, every unit of ours that has a peer unit
        //   in weapon range is ordered at the nearest one.
        if (!std::getenv("RWE_TA_HOST_ATTACK") || !taHostLink)
        {
            return;
        }
        auto& link = *taHostLink;

        std::optional<UnitId> peerUnit;
        SimScalar peerDistance;
        for (const auto& [otherUnit, other] : simulation.units)
        {
            if (other.isOwnedBy(link.remote) && other.isAlive())
            {
                peerUnit = UnitId(otherUnit);
                peerDistance = (other.position - other.position).length();
                break;
            }
        }
        if (!peerUnit)
        {
            return;
        }

        if (!link.attackSpawned)
        {
            link.attackSpawned = true;
            auto position = simulation.getUnitState(*peerUnit).position;
            position.x = position.x + SimScalar(60);
            if (auto spawned = spawnCompletedUnit(TaHostAttackUnit, localPlayerId, position))
            {
                LOG_INFO << "TA host: RWE_TA_HOST_ATTACK put " << TaHostAttackUnit << " " << spawned->value
                         << " beside the peer's unit " << peerUnit->value << " to make it shoot";
            }
            else
            {
                LOG_WARN << "TA host: RWE_TA_HOST_ATTACK could not put a " << TaHostAttackUnit
                         << " beside the peer's unit " << peerUnit->value;
            }
        }

        if (sceneTime.value < link.lastAttackOrderTick + static_cast<unsigned int>(SimTicksPerSecond))
        {
            return;
        }
        link.lastAttackOrderTick = sceneTime.value;

        for (const auto& [unit, state] : simulation.units)
        {
            if (!state.isOwnedBy(localPlayerId) || !state.isAlive())
            {
                continue;
            }

            std::optional<UnitId> nearest;
            SimScalar nearestDistance = TaHostAttackRange;
            for (const auto& [otherUnit, other] : simulation.units)
            {
                if (!other.isOwnedBy(link.remote) || !other.isAlive())
                {
                    continue;
                }
                auto distance = (other.position - state.position).length();
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearest = UnitId(otherUnit);
                }
            }
            if (!nearest)
            {
                continue;
            }

            localPlayerCommandBuffer.emplace_back(PlayerUnitCommand{
                UnitId(unit),
                PlayerUnitCommand::IssueOrder{AttackOrder(*nearest), PlayerUnitCommand::IssueOrder::Immediate}});
            ++link.attackOrders;
        }
    }

    void GameScene::logTaHostStatus()
    {
        auto& link = *taHostLink;
        const auto& driver = link.driver->stats();
        const auto& receiver = link.receiver->stats();

        std::string puppets;
        for (const auto& entry : simulation.units)
        {
            if (entry.second.isOwnedBy(link.remote) && entry.second.isAlive())
            {
                const auto& position = entry.second.position;
                puppets = " " + std::to_string(UnitId(entry.first).value) + " (" + entry.second.unitType + ") at "
                    + std::to_string(simScalarToFloat(position.x)) + "," + std::to_string(simScalarToFloat(position.z))
                    + " " + std::to_string(entry.second.hitPoints) + "hp";
                break;
            }
        }

        const auto& sender = link.sender->stats();
        LOG_INFO << "TA host: tick " << sceneTime.value << " in " << receiver.packetsApplied << " packets, clock drift "
                 << receiver.clock.driftTicks << " ticks, puppets " << driver.unitsSpawned << " up and "
                 << driver.unitsKilled << " killed, " << driver.unplacedUnits << " unplaced, taken "
                 << link.damageTaken << " and dealt " << sender.damageSent << " 0x0b, " << link.attackOrders
                 << " attacks ordered, our peer's units:" << puppets;
    }

    void GameScene::renderTaHostWindow()
    {
        if (!taHostLink)
        {
            return;
        }
        auto& link = *taHostLink;

        // An arena run draws nothing and ImGui has no frame open; the demo
        // window escapes the same fate for the same reason.
        if (gameParameters.aiArenaSeconds)
        {
            return;
        }

        const auto& driver = link.driver->stats();
        const auto& receiver = link.receiver->stats();
        const auto& sender = link.sender->stats();

        auto lobby = link.host->lobbyState();

        ImGui::SetNextWindowSize(ImVec2(440.0f, 190.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("TA peer", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::End();
            return;
        }

        if (lobby.launched)
        {
            ImGui::Text(
                "peer '%s'  %s  colour %d  team %d",
                lobby.launched->name.c_str(),
                lobby.launched->side == TadSide::Core ? "CORE" : "ARM",
                static_cast<int>(lobby.launched->colour),
                static_cast<int>(lobby.launched->team));
        }

        ImGui::Text(
            "in %llu / applied %llu / late %llu / dup %llu / held %u",
            (unsigned long long)receiver.packetsReceived,
            (unsigned long long)receiver.packetsApplied,
            (unsigned long long)receiver.packetsLate,
            (unsigned long long)receiver.packetsDuplicate,
            receiver.held);
        ImGui::Text("clock drift %lld ticks, %+.3f per 1000", (long long)receiver.clock.driftTicks, receiver.clock.driftPer1000Ticks);
        ImGui::Text(
            "puppets %llu up, %llu killed, %llu unplaced, %llu bad",
            (unsigned long long)driver.unitsSpawned,
            (unsigned long long)driver.unitsKilled,
            (unsigned long long)driver.unplacedUnits,
            (unsigned long long)driver.recordsDroppedUnknownUnit);
        ImGui::Text(
            "out %llu batches, %llu 0x0b, %llu 0x0c, %llu refused",
            (unsigned long long)sender.batches,
            (unsigned long long)sender.damageSent,
            (unsigned long long)sender.deathsSent,
            (unsigned long long)sender.unitsRefused);
        ImGui::Text(
            "damage taken %u, dropped %llu",
            link.damageTaken,
            (unsigned long long)link.host->stats().inboundDropped);

        ImGui::End();
    }

    void GameScene::endTaHost()
    {
        if (!taHostLink)
        {
            return;
        }
        auto host = taHostLink->host;

        // The simulation's hooks are the sender's, and the sender is about to
        // go: the table goes back before it is dropped.
        simulation.setTaLiveSender(TaLiveSenderHooks{});
        taHostLink.reset();

        host->leave();
    }
}
