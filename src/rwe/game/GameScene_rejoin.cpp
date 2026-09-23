#include "GameScene.h"

#include <cstdlib>
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

        if (catchUp.lastTick + 1 < atTick)
        {
            // The recording stops short of where this peer has to arrive, and
            // the ticks between are ones nobody can supply: the peers that
            // stayed resume their streams at the rejoin tick and have nothing
            // below it left to send. Refuse here rather than run on and
            // desync, which is the same trade the drop's own refusals make.
            throw std::runtime_error(
                "Rejoin bundle ends at tick " + std::to_string(catchUp.lastTick)
                + " but the rejoin is at tick " + std::to_string(atTick));
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
        gameNetworkService->start();
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
            // Nothing to bring back yet.
            return;
        }

        if (playerCommandService->droppingPlayerFor(player) != localPlayerId)
        {
            // Exactly one peer may say this, by the same rule that decides who
            // may drop them -- two rejoins naming two ticks would reopen the
            // stream in two places.
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
        auto atTick = sceneTime.value + RejoinTickMargin;
        LOG_INFO << "Asking for player " << player.value << " to rejoin at tick " << atTick;
        localPlayerCommandBuffer.push_back(PlayerRejoinedCommand{player, atTick});
        rejoinBundleDue.emplace(player.value, atTick);
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
                it = rejoinBundleDue.erase(it);
                continue;
            }

            // The recording is flushed record by record as it is written, so
            // what is on disk now is exactly ticks 1 to atTick-1 for every
            // player. Naming it is all this has to do: carrying it to the
            // returning peer is the lobby's job, over the connection it
            // already holds for that.
            LOG_INFO << "REJOIN-BUNDLE player=" << it->first << " tick=" << atTick
                     << " file=" << *gameParameters.recordReplayFile;
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
