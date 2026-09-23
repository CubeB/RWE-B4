#include "GameScene.h"

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
