#pragma once

#include <optional>
#include <string>
#include <utf8.h>
#include <utility>
#include <vector>

namespace rwe
{
    using ConstUtf8UncheckedIterator = utf8::unchecked::iterator<std::string::const_iterator>;
    using Utf8UncheckedIterator = utf8::unchecked::iterator<std::string::iterator>;

    using ConstUtf8Iterator = utf8::iterator<std::string::const_iterator>;
    using Utf8Iterator = utf8::iterator<std::string::iterator>;

    using ReverseConstUtf8Iterator = std::reverse_iterator<ConstUtf8Iterator>;
    using ReverseUtf8Iterator = std::reverse_iterator<Utf8Iterator>;

    std::vector<std::string> utf8Split(const std::string& str, const std::vector<unsigned int>& codePoints);
    std::vector<std::string> utf8Split(const std::string& str, unsigned int codePoint);
    std::optional<std::pair<std::string, std::string>> utf8SplitLast(const std::string& str, unsigned int codePoint);

    std::vector<std::string> split(const std::string& str, const std::vector<char>& codePoints);
    std::vector<std::string> split(const std::string& str, char codePoint);

    std::string toUpper(const std::string& str);

    ConstUtf8UncheckedIterator cUtf8UncheckedBegin(const std::string& str);
    ConstUtf8UncheckedIterator cUtf8UncheckedEnd(const std::string& str);
    Utf8UncheckedIterator utf8UncheckedBegin(const std::string& str);
    Utf8UncheckedIterator utf8UncheckedEnd(const std::string& str);

    Utf8Iterator utf8Begin(std::string& str);
    ConstUtf8Iterator utf8Begin(const std::string& str);
    ConstUtf8Iterator cUtf8Begin(const std::string& str);

    Utf8Iterator utf8End(std::string& str);
    ConstUtf8Iterator utf8End(const std::string& str);
    ConstUtf8Iterator cUtf8End(const std::string& str);

    void utf8TrimLeft(std::string& str);
    void utf8TrimRight(std::string& str);
    void utf8Trim(std::string& str);

    void utf8UncheckedTrimLeft(std::string& str);
    void utf8UncheckedTrimRight(std::string& str);
    void utf8UncheckedTrim(std::string& str);

    bool startsWith(const std::string& str, const std::string& prefix);
    bool endsWith(const std::string& str, const std::string& end);
    bool endsWithUtf8(const std::string& str, const std::string& end);

    std::string latin1ToUtf8(const std::string& str);

    /**
     * Text from outside, made safe to walk as UTF-8: valid input comes back
     * byte for byte, and anything else is read as latin1.
     *
     * The interface walks every string it draws with a *checked* iterator,
     * which throws on a byte sequence that is not UTF-8 -- and a throw from
     * inside a draw call takes the game down. TA's own data is latin1 where it
     * is not ASCII, and the TDF parser has always fallen back to that, but text
     * reaches the screen from places no TDF parser sees: the names in an HPI
     * archive's directory are bytes the archive's author chose, and a map pack
     * put together in another language is exactly the kind that carries them.
     *
     * latin1 rather than a guess at a code page, because it is the one fallback
     * that loses nothing: byte 0xE9 becomes U+00E9, so the original byte is
     * still there to be read differently later. Nothing here tries to render
     * it -- the shipped fonts have no glyph past ASCII, and the renderer
     * already draws the first glyph in place of one it does not have.
     */
    std::string ensureUtf8(const std::string& str);
}
