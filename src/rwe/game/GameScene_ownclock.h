#pragma once

#include <cstdint>
#include <rwe/game/GameSpeed.h>
#include <string>

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

    /**
     * What a TA peer's `0x19` says (BT-178). The record is the code, a subtype
     * byte and a value byte, not one 16-bit number: subtype 0 sets the pause
     * to the value's low bit (absolute, not a toggle) and subtype 1 sets the
     * game speed level to the value, 1 to 20 with 10 normal. The wire word the
     * puppet driver hands back is the two bytes little-endian, subtype in the
     * low one. Anything else says nothing RWE acts on.
     */
    struct PeerSpeedPacket
    {
        enum class Kind
        {
            Pause,
            Speed,
            Ignored,
        };

        Kind kind{Kind::Ignored};

        /** For Pause: the new pause state. */
        bool paused{false};

        /** For Speed: the new level, clamped to 1..20 as the original's setter clamps it. */
        int level{10};
    };

    PeerSpeedPacket decodePeerSpeedPacket(std::uint16_t wire);

    /**
     * Whether a TA joiner's pause may pause the own-clock host (#610). Since
     * BT-178 the start-up record every sender puts out (`00 01` after the
     * code) reads as a pause-on, and the ruling only says the senders appear
     * to start paused: whether a real TotalA.exe then sends the unpause is not
     * checked. A host that obeyed a start-up pause would sit paused for ever
     * if it never came, so a pause-on is applied only once the game has
     * simulated its first tick AND the joiner has sent an unpause of its own.
     * An unpause is always applied. Anything held back is logged by the caller.
     * Awaits a check against a real TotalA.exe.
     */
    struct PeerPauseGate
    {
        bool seenUnpause{false};
    };

    /** True when the pause state should be applied; records an unpause as it passes. */
    bool peerPauseAllowed(PeerPauseGate& gate, bool paused, bool gameStarted);

    /**
     * What the in-game exit menu and the save and load dialogs may offer
     * (#658). Restart and load rebuild the scene, and in a network game that
     * drops one player out of the lockstep (or out of the peer's clock) and
     * leaves the rest playing; save would write a state no peer could be
     * brought back to. All three are refused in a game with a remote peer, of
     * either kind. The own-clock and lockstep reasons stay two tests (ADR-0003)
     * because the message that says which applies is the useful one.
     */
    struct GameMenuGate
    {
        bool restart{true};
        bool load{true};
        bool save{true};
    };

    GameMenuGate gameMenuGate(bool ownClock, bool lockstepNetwork);

    /**
     * The line the original's speed setter prints when the level really
     * changed: "Game Speed Normal" at level 10, otherwise "Game Speed  +N" or
     * "Game Speed  -N" (two spaces, N the difference from 10, a plus sign only
     * when positive).
     */
    std::string gameSpeedMessage(int level);
}
