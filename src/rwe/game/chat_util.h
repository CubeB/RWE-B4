#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * The longest line the message bar accepts, in code points.
     *
     * A chat line rides on the same 1500-byte packet as that peer's commands
     * and hashes, and is resent until it is acked, so an unbounded line -- or
     * an unbounded backlog of them -- is a way to crowd the commands out of a
     * packet. Both ends are bounded instead: this, and MaxPendingChatMessages
     * in GameNetworkService.
     */
    constexpr std::size_t MaxChatMessageLength = 128;

    /**
     * What a chat line is allowed to contain, applied both to what is typed
     * and to what arrives from a peer.
     *
     * A line from a peer is not to be trusted to be well-formed UTF-8, so
     * invalid sequences are replaced before anything walks it; control
     * characters are dropped, having no glyph and, in a newline's case, being
     * able to break the single line the bar is; and the whole is cut to
     * MaxChatMessageLength code points and trimmed.
     */
    std::string sanitizeChatText(const std::string& text);

    /**
     * Takes typed text into the bar, up to MaxChatMessageLength code points.
     *
     * Deliberately not sanitizeChatText, which trims: a trailing space is
     * exactly what a player types in the middle of a sentence, and would
     * vanish from under them on every keystroke.
     */
    void chatTextAppend(std::string& text, const std::string& addition);

    /**
     * Removes the last code point, which is what the message bar's backspace
     * does. A code point rather than a byte: SDL hands us UTF-8, and removing
     * one byte of a multi-byte character would leave the rest of it behind.
     */
    void chatTextBackspace(std::string& text);

    /** How a line reads once it is on screen. */
    std::string formatChatLine(const std::string& senderName, const std::string& text);

    /**
     * The most characters of a formatted chat line that go on the wire. The
     * original copies 64 bytes of `<name->label> text` into its chat packet
     * (BT-176), so a long line is cut there, not at the typing limit.
     */
    constexpr std::size_t MaxChatWireLineLength = 64;

    /**
     * The tag a restricted line carries so that its readers know it was not
     * said to everybody. It is carried on the wire as its integer value; a
     * value this build does not know is read as None.
     */
    enum class ChatLabel : std::uint32_t
    {
        None = 0,
        Allies = 1,
        Enemies = 2,
    };

    ChatLabel chatLabelFromWire(std::uint32_t value);

    /** The name shown after the arrow: "Allies", "Enemies" or empty. */
    const char* chatLabelName(ChatLabel label);

    /** Who a line is for, before it is resolved against who is in the game. */
    struct ChatAddress
    {
        enum class Kind
        {
            Everyone,
            Allies,
            NonAllies,
            Players, // exactly the slots in the mask
        };

        Kind kind{Kind::Everyone};

        /** Bit n is player slot n. Only read when kind is Players. */
        std::uint32_t slots{0};
    };

    /**
     * The standing recipient setting the talk dialog's button cycles through:
     * 0 everybody, 1 allies, 2 non-allies, 3 a custom list (BT-176).
     */
    struct ChatSendMode
    {
        int mode{0};
        std::uint32_t customSlots{0};

        /** Advances the mode, wrapping from 3 back to 0. */
        void cycle() { mode = (mode + 1) % 4; }

        /** Adds or removes a slot from the custom list. */
        void toggleSlot(unsigned int slot);

        ChatAddress address() const;
        ChatLabel label() const;
    };

    /** A typed line resolved to who it is for, which label it carries and what it says. */
    struct ChatRoute
    {
        ChatAddress address;
        ChatLabel label{ChatLabel::None};
        std::string text;
    };

    /**
     * Applies the one-message override the original reads from the front of a
     * line: a first character of digit, A or E, a comma, colon or semicolon
     * second, sends this line alone to that player slot, to allies or to
     * non-allies, whatever the standing mode is. Leading spaces are skipped,
     * both prefix characters are stripped, and a line without a prefix goes
     * out as the standing mode says.
     */
    ChatRoute routeChatLine(const std::string& typed, const ChatSendMode& standing);

    /**
     * The peers a line is delivered to, out of the remote players of the game.
     * `allied(a, b)` is whether two slots are allies. A line is never
     * delivered to a slot that is not in `peers`, so a private line cannot
     * reach anyone it was not addressed to.
     */
    std::vector<unsigned int> chatRecipients(
        const ChatAddress& address,
        const std::vector<unsigned int>& peers,
        unsigned int localSlot,
        const std::function<bool(unsigned int, unsigned int)>& allied);

    /**
     * Cuts `text` so that the formatted line `<name->label> text` is at most
     * MaxChatWireLineLength code points, and trims it.
     */
    std::string capChatForWire(const std::string& senderName, ChatLabel label, const std::string& text);

    /** How a restricted line reads on screen: `name->Allies: text`. */
    std::string formatChatLine(const std::string& senderName, ChatLabel label, const std::string& text);

    /**
     * How long a HUD message stays and how many show at once (BT-206). The
     * original keeps a line for (textscroll + 1) * 30 game ticks, textscroll
     * being ten when the profile does not say, and holds textlines - 1 = nine
     * of them (a ten-slot ring that drops the oldest as the write position
     * meets the read position). Time is the game tick, so a paused game does
     * not age its lines and a faster one ages them faster in wall time.
     */
    constexpr std::uint32_t HudMessageLifetimeTicks = (10u + 1u) * 30u;
    constexpr std::size_t HudMessageLines = 9;

    /**
     * Whether a line that expires at `expiresAt` is gone at game tick `now`.
     * The original retires a line when its tick plus the delay plus one, times
     * thirty, is strictly below the tick, so a line is still there on the tick
     * it expires. A stamp further ahead than a lifetime can reach means the
     * game's clock went back (a restore), and the line goes rather than sit
     * for a game's length.
     */
    constexpr bool hudMessageExpired(std::uint32_t expiresAt, std::uint32_t now)
    {
        return expiresAt < now || expiresAt > now + HudMessageLifetimeTicks;
    }
}
