#pragma once

#include <rwe/game/GameSpeed.h>

namespace rwe
{
    /**
     * A scene's own clock: the speed it advances at and whether it is paused.
     *
     * GameScene keeps the same two as loose members for every mode; this
     * groups them so what a TA peer's `0x19` does to them can be unit-tested
     * without standing up a whole scene. See docs/TA-NETWORK.md.
     */
    struct GameClock
    {
        GameSpeed speed;
        bool paused{false};
    };

    /**
     * The clock after a TA peer's `0x19` game-speed level, which is the
     * original's own numbering: 1..20, normal at 10
     * (TOTALA-EXE-INTERFACE.md 294, and 256 on the wire is that normal step,
     * docs/TA-DEMOS.md "0x19"). RWE's steps are 0..19 with 9 normal, so the
     * peer's level is shifted down by one and clamped to the range RWE has.
     *
     * RWE follows the peer's speed so the two machines advance at the same
     * rate. What TA does when a peer ignores a speed change was never tried
     * (TA-NETWORK.md, "Open"); the hook exists for the wire side to try it
     * from. A speed change leaves the pause alone.
     */
    GameClock applyPeerGameSpeed(GameClock clock, int peerSpeedLevel);

    /** The clock after a TA peer's `0x19` pause or unpause. */
    GameClock applyPeerPause(GameClock clock, bool paused);
}
