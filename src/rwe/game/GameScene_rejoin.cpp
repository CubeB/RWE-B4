#include "GameScene.h"

#include <cstdlib>
#include <rwe/game/ControlChannel.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/util/SimpleLogger.h>

// Letting a dropped peer come back. Issue #188, the third part of #44.
//
// A drop is easy in the sense that matters: it makes a tick *easier* to reach,
// because the lost peer's commands stop being needed. A rejoin is the other
// way round -- from the tick it names, the game needs that player's commands
// again -- so every peer stalls there until the returning one has caught up
// and started sending. That stall is the design and not an accident. It is
// what makes a rejoin a lockstep event, agreed by every peer at one named
// tick, rather than a race between a catch-up and a running game.
//
// Two numbers line the streams back up, and both fall out of the one tick the
// rejoin names.
//
// The command stream is sequence-numbered absolutely: set N is the one the
// simulation runs on tick N+1, because submitCommands and pushCommands are
// called together, once per tick, and neither ever skips. So a stream that
// reopens at tick R reopens at sequence R-1, in both directions -- what this
// peer sends to the returning one and what it expects back.
//
// The sync hash stream has no such numbering on the wire; its tick is counted
// rather than carried. A returning peer therefore re-registers as a hash
// source with its own first tick, and until the comparison reaches that tick
// it is left out of it rather than waited for. See PlayerCommandService.

namespace rwe
{
    void GameScene::beginRejoin(Replay&& catchUp, unsigned int atTick)
    {
        if (atTick == 0)
        {
            throw std::runtime_error("A rejoin has to name the tick it resumes at");
        }

        LOG_INFO << "Rejoin: winding forward to tick " << (atTick - 1) << " from a recording of "
                 << catchUp.commands.size() << " ticks with commands, last at tick " << catchUp.lastTick;

        // The scene counts its ticks from zero and records each one before
        // advancing, so winding forward until the scene time reads atTick-1
        // has run ticks 0 to atTick-2 -- which is what the recording has to
        // cover. Anything less and the ticks between would be simulated empty
        // here and not anywhere else, which is a desync rather than a rejoin;
        // the peers that stayed resume at the rejoin tick and have nothing
        // below it left to send. Refuse, as the drop's own checks do.
        auto lastNeeded = atTick >= 2 ? atTick - 2 : 0;
        if (catchUp.lastTick < lastNeeded)
        {
            throw std::runtime_error(
                "Rejoin bundle ends at tick " + std::to_string(catchUp.lastTick)
                + " but a rejoin at tick " + std::to_string(atTick)
                + " needs it through tick " + std::to_string(lastNeeded));
        }

        rejoiningAtTick = atTick;

        // The viewer's own machinery, deliberately. While this is set the tick
        // loop takes every player's commands from the recording rather than
        // from the network or the local player, feeds no computer player, and
        // neither computes nor sends a sync hash -- all four of which are what
        // winding forward wants, and none of which this file has to write
        // again. What it does *not* do is the viewer's presentation: this is
        // a game being joined, not a recording being watched, so the fog and
        // the spectator's whole-map view stay as the lobby set them.
        replayPlayback = std::move(catchUp);

        // Wound rather than watched. Without this the catch-up runs at the
        // rate the recorded game did, which for a game an hour old is an hour
        // -- with every peer stalled at the rejoin tick for the whole of it.
        // The seek path fills the tick accumulator past what one frame will
        // dispatch, so the winding runs at whatever rate the machine manages,
        // and it stops itself on arrival.
        replaySeekTarget = atTick - 1;
    }

    void GameScene::finishRejoinIfCaughtUp()
    {
        if (!rejoiningAtTick || sceneTime.value + 1 < *rejoiningAtTick)
        {
            return;
        }

        // Everything below the rejoin tick has now been simulated from the
        // recording, and both of this peer's streams pick up at the rejoin
        // tick: its command sets from sequence atTick-1, which is where the
        // network service was told to start, and its sync hashes from the
        // first tick it runs live, which is this one.
        localSetsSubmitted = *rejoiningAtTick - 1;

        LOG_INFO << "Rejoin: caught up at tick " << sceneTime.value << "; playing from tick " << *rejoiningAtTick;
        printConsole("Rejoined the game", Color(252, 252, 0));

        replayPlayback.reset();
        rejoiningAtTick = std::nullopt;

        // Only now: see the comment at the other end of this, in onCreate.
        gameNetworkService->setAcceptingCommands(true);
    }

    bool GameScene::requestRejoin(PlayerId player, std::string& reason)
    {
        if (!playerCommandService->isDropped(player))
        {
            reason = "that player has not been dropped";
            return false;
        }

        if (playerCommandService->droppingPlayerFor(player) != localPlayerId)
        {
            // Exactly one peer may say this, by the same rule that decides who
            // may drop them -- two rejoins naming two ticks would reopen the
            // stream in two places. Everyone else's ask is refused here rather
            // than issued and ignored, so that a launcher is told to ask
            // somebody else instead of waiting on a bundle nobody is cutting.
            reason = "this peer is not the one to speak for that player";
            return false;
        }

        if (rejoinBundleDue.find(player.value) != rejoinBundleDue.end())
        {
            reason = "that player is already on their way back";
            return false;
        }

        auto atTick = sceneTime.value + RejoinTickMargin;
        LOG_INFO << "Asking for player " << player.value << " to rejoin at tick " << atTick;
        localPlayerCommandBuffer.push_back(PlayerRejoinedCommand{player, atTick});
        rejoinBundleDue.emplace(player.value, atTick);
        return true;
    }

    void GameScene::updateControlRequests()
    {
        for (const auto& request : getControlChannel().take())
        {
            if (request.command != "rejoin")
            {
                LOG_ERROR << "Bridge: nothing here answers to the command " << request.command;
                continue;
            }

            std::string reason;
            if (!requestRejoin(PlayerId(request.player), reason))
            {
                LOG_WARN << "Bridge: not asking for player " << request.player << " back: " << reason;
                getControlChannel().sendRejoinRefused(request.player, reason);
            }
        }
    }

    void GameScene::updateRejoinRequest()
    {
        // Read once and kept, like the chat test beside it: an environment
        // variable cannot change under a running process.
        //
        // This is the hook a launcher will replace. Everything below it is
        // what a real request drives -- who is coming back, at which tick, and
        // where the recording they need can be found -- and nothing about it
        // assumes the request came from here rather than over the bridge.
        static const auto spec = []() -> std::optional<std::pair<unsigned int, unsigned int>> {
            const auto* raw = std::getenv("RWE_REJOIN_TEST");
            if (raw == nullptr)
            {
                return std::nullopt;
            }

            std::string value(raw);
            auto colon = value.find(':');
            if (colon == std::string::npos)
            {
                LOG_ERROR << "RWE_REJOIN_TEST wants <player>:<seconds after the drop>, got: " << value;
                return std::nullopt;
            }

            return std::make_pair(
                static_cast<unsigned int>(std::stoul(value.substr(0, colon))),
                static_cast<unsigned int>(std::stoul(value.substr(colon + 1))));
        }();

        if (!spec || rejoinRequested)
        {
            return;
        }

        auto player = PlayerId(spec->first);
        if (!playerCommandService->isDropped(player))
        {
            // Nothing to bring back yet. Waited for rather than refused: this
            // runs every frame from the moment the game starts, and the drop
            // it is waiting for is the whole point of the test.
            return;
        }

        if (!rejoinRequestedAfter)
        {
            rejoinRequestedAfter = sceneTime.value + (spec->second * SimTicksPerSecond);
            return;
        }

        if (sceneTime.value < *rejoinRequestedAfter)
        {
            return;
        }

        rejoinRequested = true;
        std::string reason;
        if (!requestRejoin(player, reason))
        {
            LOG_WARN << "RWE_REJOIN_TEST: not asking for player " << player.value << " back: " << reason;
        }
    }

    void GameScene::writeRejoinBundleIfDue()
    {
        if (rejoinBundleDue.empty())
        {
            return;
        }

        for (auto it = rejoinBundleDue.begin(); it != rejoinBundleDue.end();)
        {
            auto atTick = it->second;
            if (sceneTime.value + 1 < atTick)
            {
                // The recording has to run through atTick-1 and no further,
                // which is the whole of what makes the returning peer land in
                // the right place. It gets there when this peer does.
                ++it;
                continue;
            }

            if (!replayWriter)
            {
                // Nothing keeps the commands but the recording, so without one
                // there is no bundle to hand over. Said once, loudly, because
                // the peers have already agreed to wait at this tick.
                LOG_ERROR << "Player " << it->first << " was agreed to rejoin at tick " << atTick
                          << ", but this peer is not recording a replay and so has nothing to hand them."
                          << " Start a game that may be rejoined with --record-replay.";
                getControlChannel().sendRejoinRefused(it->first, "this peer is not recording a replay");
                it = rejoinBundleDue.erase(it);
                continue;
            }

            // A finished replay of its own rather than the live recording,
            // because a tick with no commands writes nothing and only the
            // end-of-game record says how far a replay really ran. Cut here
            // and carried by the lobby, over the connection it already holds
            // for that.
            // Through the last tick actually recorded, which is the one
            // before the one about to run. Said that way rather than derived
            // from atTick, because the two are the same number only as long as
            // this fires on exactly the tick it is meant to.
            auto lastRecorded = sceneTime.value > 0 ? sceneTime.value - 1 : 0;
            auto bundlePath = replayWriter->path();
            bundlePath.replace_extension(".rejoin" + std::to_string(atTick) + ".rwereplay");
            if (!replayWriter->writeBundle(bundlePath, lastRecorded))
            {
                LOG_ERROR << "Could not write the rejoin bundle for player " << it->first
                          << " to " << bundlePath.string();
                getControlChannel().sendRejoinRefused(it->first, "the rejoin bundle could not be written");
                it = rejoinBundleDue.erase(it);
                continue;
            }

            LOG_INFO << "REJOIN-BUNDLE player=" << it->first << " tick=" << atTick
                     << " file=" << bundlePath.string();
            getControlChannel().sendRejoinBundle(it->first, atTick, bundlePath.string());
            it = rejoinBundleDue.erase(it);
        }
    }

    void GameScene::onPlayerRejoined(PlayerId player, unsigned int fromTick)
    {
        // The service applied this the moment the command arrived, for the
        // same reason the drop is applied then: what it describes is a tick
        // rather than a moment. Repeated because a caller ought not to have to
        // know that, and because it costs one lookup.
        playerCommandService->rejoinPlayer(player, fromTick);

        if (dropAnnounced.erase(player.value) > 0)
        {
            printConsole(playerDisplayName(player) + " has rejoined the game", Color(252, 252, 0));
            LOG_INFO << "Player " << player.value << " rejoined the game at tick " << fromTick;
        }

        // They may be dropped again later, and the second drop is as real as
        // the first; forgetting the first is what lets it be issued.
        dropIssued.erase(player.value);

        // The returning peer has a whole game to load and wind through before
        // it can answer, and silence is what the drop timer watches for. Left
        // to itself it would undo this rejoin within the timeout.
        rejoinGraceUntil[player.value] = getTimestamp() + RejoinGraceSeconds;

        pendingRejoins[player.value] = fromTick;
        resumeRejoiningPeers();
    }

    void GameScene::resumeRejoiningPeers()
    {
        if (pendingRejoins.empty())
        {
            return;
        }

        for (auto it = pendingRejoins.begin(); it != pendingRejoins.end();)
        {
            auto player = PlayerId(it->first);
            auto fromTick = it->second;
            auto resumeAt = fromTick > 0 ? fromTick - 1 : 0;

            if (localSetsSubmitted < resumeAt)
            {
                // Not yet: this peer's own stream has not reached the point the
                // returning one is to be given. It will, within the few ticks
                // the command buffer runs ahead by.
                ++it;
                continue;
            }

            if (player == localPlayerId)
            {
                // This peer is the one coming back. Its own stream is reopened
                // by the loading scene, which knows where to resume from before
                // there is a scene to ask; there is no forgotten endpoint here
                // to remember, this peer never having lost itself.
                it = pendingRejoins.erase(it);
                continue;
            }

            if (playerCommandService->isDropped(player))
            {
                // The rejoin was refused -- a tick already run, or one at or
                // before the cut. Nothing to listen to.
                LOG_WARN << "Not listening to player " << player.value << " again: their rejoin was refused";
                it = pendingRejoins.erase(it);
                continue;
            }

            if (!gameNetworkService->rememberPeer(player, SequenceNumber(resumeAt), SequenceNumber(resumeAt)))
            {
                // This peer no longer holds its own stream back that far, so
                // the returning one cannot be given the ticks it missed from
                // here. The game carries on stalled at the rejoin tick, which
                // is visible, rather than quietly running on without a player
                // it has just agreed to wait for.
                LOG_ERROR << "Player " << player.value << " cannot rejoin at tick " << fromTick
                          << ": this peer no longer holds its own commands from there";
            }

            it = pendingRejoins.erase(it);
        }
    }
}
