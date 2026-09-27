#include "GameScene.h"

#include <imgui.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/net/ta/TaHostGame.h>
#include <rwe/net/ta/TaOutboundBatcher.h>
#include <rwe/puppet/TadPuppetDriver.h>
#include <rwe/puppet/TaLiveReceiver.h>
#include <rwe/sim/MixedOwnership.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/TaLiveBatch.h>
#include <rwe/sim/TaLiveSender.h>
#include <rwe/sim/TaPeerIds.h>
#include <rwe/util/SimpleLogger.h>
#include <algorithm>
#include <cstdlib>
#include <optional>
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

        /** The peer unit RWE_TA_HOST_ATTACK names, once it has been read. */
        std::optional<std::uint16_t> attackTarget;
        unsigned int lastAttackOrderTick{0};
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
        peerIds.unitId = [&link](UnitId unit) { return link->driver->wireIdOf(unit); };
        peerIds.dplayId = [peerId](PlayerId player) -> std::optional<std::uint32_t> { return peerId; };

        TaLiveSenderSettings senderSettings;
        senderSettings.sender = localPlayerId;
        senderSettings.maxUnits = config.maxUnits;
        senderSettings.firstBlock = TaHostGame::hostUnitBlock;
        senderSettings.unitLoadOrder = unitLoadOrder;
        link->sender = std::make_unique<TaLiveSender>(simulation, std::move(senderSettings), std::move(peerIds));
        simulation.setTaLiveSender(link->sender->hooks());

        link->traffic = std::make_unique<TaOutboundBatcher>(
            [&link](TaHostSession::PeerId id, std::span<const std::uint8_t> bytes, TaTransport transport) {
                link->host->queueOutbound(id, bytes, transport);
            },
            TadPacketCompressed);

        // A 0x0b naming a unit of ours is the attacker's to report, and the
        // driver does not puppet it: the wire id is turned back into the unit
        // it is and applied through the ordinary damage path, so the local
        // Killed script picks the death and the 0x0c goes out.
        link->driver->setIncomingDamageHandler(
            [this, &link](std::uint16_t victimId, std::optional<UnitId> attacker, unsigned int damage) {
                auto it = link->localByWireId.find(victimId);
                if (it == link->localByWireId.end())
                {
                    link->localByWireId.clear();
                    for (const auto& [unit, state] : simulation.units)
                    {
                        if (auto id = link->sender->wireIdOf(UnitId(unit)))
                        {
                            link->localByWireId.emplace(*id, UnitId(unit));
                        }
                    }
                    it = link->localByWireId.find(victimId);
                    if (it == link->localByWireId.end())
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

                ++link->damageTaken;
                applyIncomingDamage(simulation, victim, attacker, damage);
                LOG_INFO << "TA host: our unit " << victim.value << " took " << damage << " from the peer";
            });

        taHostLink = std::move(link);

        LOG_INFO << "TA host: '" << config.gameName << "' on " << config.mapName << ", the joiner is player "
                 << remotePlayer.value << " on peer 0x" << std::hex << link->peer << std::dec << ", "
                 << config.maxUnits << " units a block, ours in block " << TaHostGame::hostUnitBlock;
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
        // Testing aid: RWE_TA_HOST_ATTACK=<the peer's wire unit id> orders the
        // local player's first unit at that unit once a second, which is how
        // RWE's own 0x0b can be seen reaching the peer without a player at the
        // keyboard. Like RWE_DEBUG_SPAWN, it is in the environment rather than
        // on the command line, and it orders nothing but an attack.
        const char* attack = std::getenv("RWE_TA_HOST_ATTACK");
        if (!attack || !taHostLink)
        {
            return;
        }
        auto& link = *taHostLink;

        if (!link.attackTarget)
        {
            link.attackTarget = static_cast<std::uint16_t>(std::atoi(attack));
        }
        if (sceneTime.value < link.lastAttackOrderTick + static_cast<unsigned int>(SimTicksPerSecond))
        {
            return;
        }
        link.lastAttackOrderTick = sceneTime.value;

        auto target = link.driver->unitOfWireId(*link.attackTarget);
        if (!target)
        {
            return;
        }
        for (const auto& [unit, state] : simulation.units)
        {
            if (!state.isOwnedBy(localPlayerId) || !state.isAlive())
            {
                continue;
            }
            localPlayerCommandBuffer.emplace_back(PlayerUnitCommand{
                UnitId(unit),
                PlayerUnitCommand::IssueOrder{AttackOrder(*target), PlayerUnitCommand::IssueOrder::Immediate}});
            break;
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

        LOG_INFO << "TA host: tick " << sceneTime.value << " in " << receiver.packetsApplied << " packets, clock drift "
                 << receiver.clock.driftTicks << " ticks, puppets " << driver.unitsSpawned << " up and "
                 << driver.unitsKilled << " killed, " << driver.unplacedUnits << " unplaced, damage taken "
                 << link.damageTaken << ", our peer's units:" << puppets;
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
            "out %llu batches, %llu refused, taken %u, dropped %llu",
            (unsigned long long)sender.batches,
            (unsigned long long)sender.unitsRefused,
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
