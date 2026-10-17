#include "GameScene_ownclock.h"
#include "GameScene.h"
#include <algorithm>
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

    PeerSpeedPacket decodePeerSpeedPacket(std::uint16_t wire)
    {
        PeerSpeedPacket packet;
        auto subtype = static_cast<unsigned int>(wire & 0xFFu);
        auto value = static_cast<unsigned int>(wire >> 8);
        if (subtype == 0)
        {
            packet.kind = PeerSpeedPacket::Kind::Pause;
            packet.paused = (value & 1u) != 0;
        }
        else if (subtype == 1)
        {
            packet.kind = PeerSpeedPacket::Kind::Speed;
            packet.level = static_cast<int>(std::clamp(value, 1u, 20u));
        }
        return packet;
    }

    bool peerPauseAllowed(PeerPauseGate& gate, bool paused, bool gameStarted)
    {
        if (!paused)
        {
            gate.seenUnpause = true;
            return true;
        }
        return gameStarted && gate.seenUnpause;
    }

    std::string gameSpeedMessage(int level)
    {
        auto offset = std::clamp(level, 1, 20) - 10;
        if (offset == 0)
        {
            return "Game Speed Normal";
        }
        return std::string("Game Speed  ") + (offset > 0 ? "+" : "") + std::to_string(offset);
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

    GameMenuGate gameMenuGate(bool ownClock, bool lockstepNetwork)
    {
        GameMenuGate gate;
        if (ownClock || lockstepNetwork)
        {
            gate.restart = false;
            gate.load = false;
            gate.save = false;
        }
        return gate;
    }

    bool GameScene::isLockstepNetworkGame() const
    {
        return !isPlayback() && !isOwnClock() && gameNetworkService->hasRemotePeers();
    }

    bool GameScene::canSave() const
    {
        // An own-clock game has a peer whose units and clock RWE does not
        // own, so a save of RWE's half could not be resumed into the game it
        // came from. The lockstep save is untouched.
        return !isOwnClock();
    }
}
