#include "GameScene.h"

#include <algorithm>
#include <rwe/game/ControlChannel.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>

// Surviving a peer that stops answering: noticing, saying so on screen, and
// carrying on without it. Issue #44, parts one and two.
//
// The shape of it is set by the one thing lockstep cannot do: a tick needs
// every player's commands for that tick, so a peer that stops sending stops
// the game for everybody, and the command stream is exactly the channel that
// has gone quiet. The drop therefore travels as an ordinary command from a
// peer that is still talking, and is applied the moment it arrives rather than
// when it is popped -- see PlayerDroppedCommand, and PlayerCommandService's
// dropPlayer for why applying it early is safe.

namespace rwe
{
    std::string GameScene::playerDisplayName(PlayerId playerId) const
    {
        const auto& player = simulation.getPlayer(playerId);
        if (player.name && !player.name->empty())
        {
            return *player.name;
        }

        return "Player " + std::to_string(playerId.value + 1);
    }

    bool GameScene::onlyComputerPlayersAreNotReady() const
    {
        // True when the only buffers still empty belong to computer players,
        // which is to say: the tick will run as soon as the AI's own commands
        // are queued, and queueing them is not a guess about a tick that may
        // never happen.
        for (const auto& playerId : playerCommandService->playersNotReady())
        {
            if (simulation.getPlayer(playerId).type != GamePlayerType::Computer)
            {
                return false;
            }
        }

        return true;
    }

    bool GameScene::localHumanCommandsAreFedPerTick() const
    {
        return !replayPlayback
            && !gameNetworkService->hasRemotePeers()
            && simulation.getPlayer(localPlayerId).type == GamePlayerType::Human;
    }

    unsigned int GameScene::chooseDropTick(const std::vector<GameNetworkService::PeerStatus>& peers) const
    {
        // Past the furthest tick anybody has reached, not merely past this
        // peer's. A peer that ran further than the one issuing the drop would
        // otherwise have simulated commands from the lost player that the cut
        // is about to throw away, and that is a desync already in the past by
        // the time anyone could notice it.
        auto tick = sceneTime.value;
        for (const auto& peer : peers)
        {
            if (peer.lastKnownSceneTime)
            {
                tick = std::max(tick, peer.lastKnownSceneTime->value);
            }
        }

        return tick + DropTickMargin;
    }

    void GameScene::updatePeerLiveness()
    {
        // A recording has no peers; its every player is fed from the file.
        if (replayPlayback)
        {
            return;
        }

        // A returning peer is listened to again as soon as this peer's own
        // stream has reached the point it is to be given, which is ordinarily
        // a few ticks after the rejoin was agreed.
        resumeRejoiningPeers();

        waitingForPlayers = playerCommandService->playersNotReady();

        // A lone human is fed a tick at a time when the tick runs, so their
        // buffer being empty between ticks is not a peer gone quiet and must
        // not raise the waiting notice. See localHumanCommandsAreFedPerTick.
        if (localHumanCommandsAreFedPerTick())
        {
            waitingForPlayers.erase(
                std::remove(waitingForPlayers.begin(), waitingForPlayers.end(), localPlayerId),
                waitingForPlayers.end());
        }

        if (waitingForPlayers.empty())
        {
            waitingSince = std::nullopt;
            dropCountdownSeconds = std::nullopt;
            return;
        }

        if (!waitingSince)
        {
            waitingSince = getTimestamp();
        }

        auto timeoutSeconds = sceneContext.globalConfig->dropTimeoutSeconds;
        if (timeoutSeconds == 0)
        {
            // Dropping switched off: wait for ever, which is what the game did
            // before there was a timeout and what somebody who would rather
            // keep a reconnecting player than lose them will want.
            dropCountdownSeconds = std::nullopt;
            return;
        }

        auto timeout = std::chrono::milliseconds(timeoutSeconds * 1000);

        // Only asked for while a tick is actually held up, which is rare: the
        // buffers are kept several ticks deep and only run dry when a peer
        // stops feeding them. It costs a round trip to the network thread.
        auto peers = gameNetworkService->getPeerStatuses();

        std::optional<unsigned int> soonest;
        for (const auto& peer : peers)
        {
            if (std::find(waitingForPlayers.begin(), waitingForPlayers.end(), peer.playerId) == waitingForPlayers.end())
            {
                // Present, and merely a little behind. That is ordinary.
                continue;
            }

            if (auto grace = rejoinGraceUntil.find(peer.playerId.value); grace != rejoinGraceUntil.end())
            {
                if (peer.silence < timeout)
                {
                    // It is talking again: the rejoin has landed, and the
                    // ordinary timer can have it back.
                    rejoinGraceUntil.erase(grace);
                }
                else if (getTimestamp() < grace->second)
                {
                    // Still expected. Nothing is counted down at it, because
                    // what a player watching wants to know is that somebody is
                    // on their way back rather than how long until they are
                    // given up on again.
                    continue;
                }
                else
                {
                    LOG_WARN << "Player " << peer.playerId.value << " was expected to rejoin and has not";
                    rejoinGraceUntil.erase(grace);
                }
            }

            if (peer.silence < timeout)
            {
                auto remaining = std::chrono::duration_cast<std::chrono::seconds>(timeout - peer.silence).count() + 1;
                auto remainingSeconds = static_cast<unsigned int>(remaining);
                soonest = soonest ? std::min(*soonest, remainingSeconds) : remainingSeconds;
                continue;
            }

            // Gone. Exactly one peer may say so, or two drops naming two ticks
            // would cut the stream in two places; every peer works out the same
            // one from state they all share.
            if (playerCommandService->droppingPlayerFor(peer.playerId) != localPlayerId)
            {
                continue;
            }

            if (!dropIssued.insert(peer.playerId.value).second)
            {
                // Already asked for. The command is on its way and the drop
                // will be applied the moment it lands, here as well as away.
                continue;
            }

            auto fromTick = chooseDropTick(peers);
            LOG_INFO << "Player " << peer.playerId.value << " has been quiet for " << peer.silence.count()
                     << " ms; dropping them from tick " << fromTick;
            localPlayerCommandBuffer.push_back(PlayerDroppedCommand{peer.playerId, fromTick});
        }

        dropCountdownSeconds = soonest;
    }

    void GameScene::renderWaitingForPlayers()
    {
        if (waitingForPlayers.empty() || !waitingSince)
        {
            return;
        }

        // A packet arriving a frame late empties a buffer for a frame or two in
        // any ordinary game. Saying so would make the caption flicker through a
        // healthy match, so it has to have lasted.
        if (getTimestamp() - *waitingSince < WaitingCaptionDelay)
        {
            return;
        }

        auto centerX = static_cast<float>(sceneContext.viewport->width()) / 2.0f;
        auto centerY = static_cast<float>(sceneContext.viewport->height()) / 2.0f;

        // The pause banner's own presentation, which is where a player's eye
        // already goes when the game stops, with the reason written under it.
        auto title = gameMediaDatabase.getSpriteSeries("IGTITLES", "igpaused");
        auto lineY = centerY;
        if (title && !(*title)->sprites.empty())
        {
            const auto& sprite = *(*title)->sprites.front();
            chromeUiRenderService.drawSpriteAbs(
                std::floor(centerX - (sprite.bounds.width() / 2.0f)),
                std::floor(centerY - (sprite.bounds.height() / 2.0f)),
                sprite);
            lineY = std::floor(centerY + (sprite.bounds.height() / 2.0f) + 12.0f);
        }

        std::string names;
        for (const auto& playerId : waitingForPlayers)
        {
            if (!names.empty())
            {
                names += ", ";
            }
            names += playerDisplayName(playerId);
        }

        chromeUiRenderService.drawTextCenteredX(centerX, lineY, "WAITING FOR " + names, *guiFont);

        if (dropCountdownSeconds)
        {
            chromeUiRenderService.drawTextCenteredX(
                centerX,
                lineY + 14.0f,
                "DROPPING IN " + std::to_string(*dropCountdownSeconds),
                *guiFont);
        }
    }

    void GameScene::onPlayerDropped(PlayerId player, unsigned int fromTick)
    {
        // A no-op: the service applied this the moment the command arrived,
        // because that is what let the tick carrying it run at all. Repeated
        // because a caller ought not to have to know that, and because it costs
        // one lookup.
        playerCommandService->dropPlayer(player, fromTick);

        if (dropAnnounced.insert(player.value).second)
        {
            // Their units are left exactly as they are, with the orders and the
            // fire mode they had: that is what "their units stay on the map"
            // means in the original, and it is the least new behaviour.
            printConsole(playerDisplayName(player) + " has left the game", Color(252, 252, 0));
            LOG_INFO << "Player " << player.value << " left the game at tick " << fromTick;

            // The launcher hears it too, if one is listening: it is the half of
            // this that can offer the player their seat back.
            getControlChannel().sendPlayerDropped(player.value, fromTick);
        }

        gameNetworkService->forgetPeer(player);

        // Nothing is waiting on them any more, so the caption goes at once
        // rather than a frame later.
        waitingForPlayers.erase(
            std::remove(waitingForPlayers.begin(), waitingForPlayers.end(), player),
            waitingForPlayers.end());
        if (waitingForPlayers.empty())
        {
            waitingSince = std::nullopt;
            dropCountdownSeconds = std::nullopt;
        }
    }
}
