#pragma once

#include <cstddef>
#include <string>

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
}
