#include "GameScene_ownclock.h"
#include "GameScene.h"
#include <rwe/util/SimpleLogger.h>

// The own-clock game: a live game whose scene advances on its own clock like
// a skirmish, with no lockstep command gate, no sync-hash exchange, no desync
// report and no save. It exists for playing against a peer that runs its own
// simulation and waits for nobody -- a real TotalA.exe, which is
// owner-authoritative rather than lockstep (docs/TA-NETWORK.md). Split out of
// GameScene.cpp for the reason set out at the head of GameScene_render.cpp:
// the single translation unit had outgrown the sections a COFF object can
// address.

namespace rwe
{
    GameClock applyPeerGameSpeed(GameClock clock, int peerSpeedLevel)
    {
        clock.speed = GameSpeed(peerSpeedLevel - 1);
        return clock;
    }

    GameClock applyPeerPause(GameClock clock, bool paused)
    {
        clock.paused = paused;
        return clock;
    }

    void GameScene::setPeerGameSpeedLevel(int peerSpeedLevel)
    {
        // Only an own-clock game takes its speed from the peer; a lockstep
        // game's speed arrives as an ordinary host-authoritative command.
        if (!isOwnClock())
        {
            return;
        }

        auto clock = applyPeerGameSpeed(GameClock{gameSpeed, paused}, peerSpeedLevel);
        gameSpeed = clock.speed;
        paused = clock.paused;
        LOG_INFO << "Peer set game speed to level " << peerSpeedLevel << " (step " << gameSpeed.index() << ")";
    }

    void GameScene::setPeerPaused(bool paused)
    {
        if (!isOwnClock())
        {
            return;
        }

        auto clock = applyPeerPause(GameClock{gameSpeed, paused}, paused);
        gameSpeed = clock.speed;
        this->paused = clock.paused;
        LOG_INFO << "Peer " << (paused ? "paused" : "unpaused") << " the game";
    }

    bool GameScene::canSave() const
    {
        // An own-clock game has a peer whose units and clock RWE does not
        // own, so a save of RWE's half could not be resumed into the game it
        // came from. The lockstep save is untouched.
        return !isOwnClock();
    }
}
