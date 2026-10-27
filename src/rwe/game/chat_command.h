#pragma once

#include <optional>
#include <string>

namespace rwe
{
    /**
     * A line typed into the chat bar that starts with `+`, read the way the
     * original's command interpreter reads it (TOTALA-EXE-COMMANDS.md section
     * 124). Pure text in, a description out: nothing here knows about the
     * simulation, so it can be tested without one.
     *
     * The interpreter tokenises on whitespace, stops at `#`, has no quoting,
     * and keeps at most 20 tokens. The command word is matched without regard
     * to case. An integer argument that is missing is 0, and one that is
     * present is read as `atoi` reads it, so `12abc` is 12 and `abc` is 0.
     *
     * Only the sharing commands are built. Every other word the original
     * registers (NoShake, Contour, ... ) is Unknown here, and so is
     * `+ShareLOS`, which the original does not register either.
     */
    struct ChatCommand
    {
        enum class Kind
        {
            /** `+` and nothing else: nothing to run, nothing to say. */
            Empty,
            /** A word this build does not know; `word` is what was typed. */
            Unknown,
            ShareMetal,
            ShareEnergy,
            /** The metal and the energy toggles, in that order. */
            ShareAll,
            /** Accepted, but there is no toggle: allies always share sight. */
            ShareMapping,
            /** Accepted, but there is no toggle: radar and sonar are never shared with allies. */
            ShareRadar,
            /** `+SetShareMetal n`: `number` is what was typed. */
            SetShareMetal,
            /** `+SetShareEnergy n`: `number` is what was typed. */
            SetShareEnergy,
            /** `+Give player amount metal|energy`. */
            Give,
            /** `+Give` with an argument list that does not make a gift. */
            GiveUsage,
            /** `+Now ...`: the developer phrase; see `isDeveloperDebugPhrase`. */
            Now,
        };

        Kind kind{Kind::Empty};

        /**
         * `Now`: true only for the exact phrase
         * `Now Film Chris Include Reload Assert` -- the word is
         * case-insensitive, the five after it are not. A failed phrase clears
         * the debug unlock (#906).
         */
        bool developerPhraseAccepted{false};

        /** The command word as typed, without the `+`. */
        std::string word;

        /** SetShare*: the reserve typed. Give: the amount. */
        int number{0};

        /** Give: the first argument as typed (a player's number or name). */
        std::string playerToken;

        /** Give: metal if true, energy if false. */
        bool giveMetal{true};
    };

    /**
     * Reads a line of chat. Returns nothing when the first non-space
     * character is not `+`, which is how the caller knows it is ordinary chat;
     * when it is `+` the line is a command whatever follows, and is never
     * to be sent as chat.
     */
    std::optional<ChatCommand> parseChatCommand(const std::string& line);

    /** Permission bits, numbered as the original's tables are (#906). */
    enum ChatCommandMask : unsigned int
    {
        ChatCommandMaskOrdinary = 1,
        ChatCommandMaskCheat = 2,
        ChatCommandMaskDebug = 4,
    };

    /**
     * The mask a locally typed `+` line runs under (#906,
     * BT-GAME-COMMAND-MASK-GATING-FINDINGS.md): 7 once the local developer
     * phrase has unlocked debug, otherwise 1 in a campaign and 3 elsewhere.
     * Multiplayer uses the host's Cheat Codes setting when the game has one;
     * RWE has no such lobby option, so a caller passes false there.
     *
     * This is presentation state: it is never hashed, saved or replicated.
     */
    unsigned int chatCommandMask(bool debugUnlocked, bool campaign, bool multiplayer, bool cheatCodesEnabled);

    /**
     * What +ShareMapping and +ShareRadar print. RWE has neither toggle: allies
     * always share what they see (BT-57), and since BT-139 nobody shares a
     * radar or sonar picture, so the old line "allies already share their
     * radar" was false.
     */
    std::string shareNoticeMessage(ChatCommand::Kind kind);

    /**
     * `atoi` without its undefined behaviour: optional leading white space,
     * an optional sign, then digits up to the first character that is not
     * one; nothing numeric at the start is 0, and a value outside `int`
     * saturates rather than wrapping.
     */
    int chatAtoi(const std::string& token);
}
