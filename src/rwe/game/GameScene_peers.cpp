#include "GameScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <rwe/game/ControlChannel.h>
#include <rwe/sim/SimTicksPerSecond.h>
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

        auto centerX = static_cast<float>(sceneContext.viewport->width()) / (2.0f * static_cast<float>(effectiveUiScale()));
        auto centerY = static_cast<float>(sceneContext.viewport->height()) / (2.0f * static_cast<float>(effectiveUiScale()));

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

    float GameScene::roundTripLowerBound(const GameNetworkService::PeerStatus& peer)
    {
        // Less the time the peer may have held the ack before sending it.
        auto waited = static_cast<float>((peer.oldestUnackedAge - GameNetworkService::SendInterval).count());
        return std::max(peer.latestRoundTripMillis, waited);
    }

    void GameScene::recordNetworkHistory()
    {
        if (gameParameters.aiArenaSeconds)
        {
            return;
        }

        // A round trip to the network thread every frame, the same as the one
        // update() already makes for the buffer depth.
        latestPeerStatuses = gameNetworkService->getPeerStatuses();

        std::vector<PeerSample> samples;
        samples.reserve(latestPeerStatuses.size());
        for (const auto& peer : latestPeerStatuses)
        {
            std::optional<float> lead;
            if (peer.estimatedSceneTimeNow)
            {
                lead = *peer.estimatedSceneTimeNow - static_cast<float>(sceneTime.value);
            }

            samples.push_back(PeerSample{
                peer.playerId,
                roundTripLowerBound(peer),
                playerCommandService->bufferedCommandCount(peer.playerId),
                static_cast<float>(peer.silence.count()),
                static_cast<float>(peer.unackedCommandSets),
                lead});
        }

        auto now = getTimestamp();
        networkHistory.observe(now, sceneTime.value, lockstepStats.stalledSoFar(now), samples);
    }

    void GameScene::renderNetworkOverlay()
    {
        // An arena run has no ImGui frame open; see renderReplayWindow.
        if (!networkOverlayVisible || gameParameters.aiArenaSeconds)
        {
            return;
        }

        const ImVec4 warn(1.0f, 0.8f, 0.2f, 1.0f);
        const ImVec4 bad(1.0f, 0.35f, 0.3f, 1.0f);
        const ImVec4 plain = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        auto colour = [&](bool isBad, bool isWarn) {
            if (isBad)
            {
                return bad;
            }
            return isWarn ? warn : plain;
        };

        const ImVec2 graphSize(280.0f, 34.0f);
        auto plot = [&](const char* id, const SampleRing& ring, const std::string& caption, float scaleMax) {
            ImGui::PlotLines(id, ring.data(), static_cast<int>(ring.size()), static_cast<int>(ring.offset()), caption.c_str(), 0.0f, scaleMax, graphSize);
        };

        auto names = [&](const std::vector<PlayerId>& players) {
            std::string s;
            for (const auto& p : players)
            {
                s += (s.empty() ? "" : ", ") + playerDisplayName(p);
            }
            return s;
        };

        const auto& displaySize = ImGui::GetIO().DisplaySize;
        // Below the top bar, which is laid out in the scene's viewport units
        // rather than ImGui's.
        auto belowTopBar = (static_cast<float>(GuiSizeTop) + 8.0f) * displaySize.y / static_cast<float>(sceneContext.viewport->height());
        ImGui::SetNextWindowPos(ImVec2(displaySize.x - 8.0f, belowTopBar), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.45f);
        const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        if (!ImGui::Begin("Network overlay", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        auto now = getTimestamp();
        auto expected = static_cast<float>(SimTicksPerSecond * gameSpeed.perMille() / 1000);

        // Every figure below is read off the history, which moves four times a
        // second, rather than off this frame, which moves too fast to read.
        const auto& tickRate = networkHistory.tickRate();
        auto tickCaption = paused ? std::string("paused") : std::to_string(static_cast<int>(tickRate.latest() + 0.5f)) + " of " + std::to_string(static_cast<int>(expected)) + " ticks/s";
        ImGui::Text("tick %u", sceneTime.value);
        plot("##ticks", tickRate, tickCaption, expected * 1.5f);

        if (!replayPlayback)
        {
            auto worstAverage = 0.0f;
            for (const auto& peer : latestPeerStatuses)
            {
                worstAverage = std::max(worstAverage, peer.averageRoundTripMillis);
            }
            auto target = commandBufferTargetForRttMillis(worstAverage);
            ImGui::Text("order delay %u ticks (%d ms)", target, static_cast<int>(target) * SimMillisecondsPerTick);
        }

        const auto& stalled = networkHistory.stalled();
        auto stallCaption = "stalls " + std::to_string(lockstepStats.stallCount()) + ", longest " + std::to_string(lockstepStats.longestStall().count()) + " ms";
        ImGui::PlotHistogram("##stalls", stalled.data(), static_cast<int>(stalled.size()), static_cast<int>(stalled.offset()), stallCaption.c_str(), 0.0f, static_cast<float>(NetworkHistory::Interval.count()), graphSize);

        if (auto stall = lockstepStats.currentStall(now); stall && *stall >= LockstepStats::CountedStall)
        {
            ImGui::TextColored(bad, "STALLED %lld ms, waiting for %s", static_cast<long long>(stall->count()), names(lockstepStats.lastWaitingFor()).c_str());
        }
        else if (!lockstepStats.lastWaitingFor().empty())
        {
            ImGui::Text("last stall waited for %s", names(lockstepStats.lastWaitingFor()).c_str());
        }

        if (replayPlayback)
        {
            ImGui::End();
            return;
        }

        if (latestPeerStatuses.empty())
        {
            ImGui::Text("no remote players");
            ImGui::End();
            return;
        }

        for (const auto& series : networkHistory.peers())
        {
            if (!series.present)
            {
                continue;
            }

            auto status = std::find_if(latestPeerStatuses.begin(), latestPeerStatuses.end(), [&](const auto& s) { return s.playerId == series.playerId; });
            if (status == latestPeerStatuses.end())
            {
                continue;
            }

            ImGui::PushID(static_cast<int>(series.playerId.value));
            ImGui::Separator();

            auto quiet = series.quiet.latest();
            auto lead = series.tickLead.latest();
            auto unacked = series.unacked.latest();
            auto perSecond = static_cast<float>(SimTicksPerSecond);
            ImGui::TextUnformatted(playerDisplayName(series.playerId).c_str());
            ImGui::SameLine();
            // A peer sends every 100 ms whether it has anything to say or not.
            ImGui::TextColored(colour(quiet > 1000.0f, quiet > 300.0f), "quiet %.0f", quiet);
            ImGui::SameLine();
            // Peers are held within three ticks of each other before the
            // catch-up logic steps in.
            ImGui::TextColored(colour(std::abs(lead) > 10.0f, std::abs(lead) > 3.0f), "tick %+.0f", lead);
            ImGui::SameLine();
            // Sets go out about one a tick and wait at least a send interval and
            // a round trip to be acknowledged, so a handful is normal and a
            // second's worth is not.
            ImGui::TextColored(colour(unacked > 3.0f * perSecond, unacked > perSecond), "unacked %.0f", unacked);

            // Coloured by the worst recent sample, not the average, because the
            // spike is what stalls a game.
            // With a margin, because timer slop puts the bound a few ms over
            // the last sample in a perfectly healthy game.
            auto awaitingAck = roundTripLowerBound(*status) > status->latestRoundTripMillis + static_cast<float>(GameNetworkService::SendInterval.count());
            ImGui::PushStyleColor(ImGuiCol_PlotLines, colour(awaitingAck || status->maxRoundTripMillis > 500.0f, status->maxRoundTripMillis > 250.0f));
            char rttCaption[64];
            if (awaitingAck)
            {
                std::snprintf(rttCaption, sizeof(rttCaption), "rtt >= %.0f ms, no ack", roundTripLowerBound(*status));
            }
            else
            {
                std::snprintf(rttCaption, sizeof(rttCaption), "rtt %.0f ms (%.0f-%.0f)", status->averageRoundTripMillis, status->minRoundTripMillis, status->maxRoundTripMillis);
            }
            plot("##rtt", series.roundTrip, rttCaption, std::max(200.0f, series.roundTrip.max()));
            ImGui::PopStyleColor();

            auto buffered = series.buffered.latest();
            ImGui::PushStyleColor(ImGuiCol_PlotLines, colour(buffered < 1.0f, buffered <= 2.0f));
            plot("##buffered", series.buffered, "buffered " + std::to_string(static_cast<int>(buffered)), std::max(1.0f, series.buffered.max()));
            ImGui::PopStyleColor();

            ImGui::PopID();
        }

        ImGui::End();
    }
}
