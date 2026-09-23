#include <catch2/catch_test_macros.hpp>
#include <rwe/game/chat_util.h>

namespace rwe
{
    TEST_CASE("sanitizeChatText")
    {
        SECTION("leaves an ordinary line alone")
        {
            REQUIRE(sanitizeChatText("attacking from the north") == "attacking from the north");
        }

        SECTION("drops control characters")
        {
            REQUIRE(sanitizeChatText("two\nlines\tand a \x01 bell") == "twolinesand a  bell");
        }

        SECTION("drops the delete character")
        {
            REQUIRE(sanitizeChatText("go\x7fod") == "good");
        }

        SECTION("trims the ends")
        {
            REQUIRE(sanitizeChatText("   hello   ") == "hello");
        }

        SECTION("keeps a line of exactly the maximum length")
        {
            std::string line(MaxChatMessageLength, 'a');
            REQUIRE(sanitizeChatText(line) == line);
        }

        SECTION("cuts a line longer than the maximum")
        {
            std::string line(MaxChatMessageLength + 10, 'a');
            REQUIRE(sanitizeChatText(line).size() == MaxChatMessageLength);
        }

        SECTION("counts the cut in code points, not bytes")
        {
            // Each of these is two bytes, so a byte count would cut the line
            // in half -- and in the middle of a character.
            std::string line;
            for (std::size_t i = 0; i < MaxChatMessageLength; ++i)
            {
                line += "\xc3\xa9";
            }

            REQUIRE(sanitizeChatText(line) == line);
        }

        SECTION("replaces a malformed sequence rather than throwing")
        {
            // A lone continuation byte: what a hostile or simply broken peer
            // can put on the wire, and what must not reach the renderer.
            auto result = sanitizeChatText("ok\x80then");
            REQUIRE(result == "ok?then");
        }

        SECTION("is empty for a line with nothing printable in it")
        {
            REQUIRE(sanitizeChatText("\n\t  \n").empty());
        }
    }

    TEST_CASE("chatTextBackspace")
    {
        SECTION("removes the last character")
        {
            std::string text = "abc";
            chatTextBackspace(text);
            REQUIRE(text == "ab");
        }

        SECTION("removes a whole multi-byte character")
        {
            std::string text = "a\xc3\xa9";
            chatTextBackspace(text);
            REQUIRE(text == "a");
        }

        SECTION("does nothing to an empty line")
        {
            std::string text;
            chatTextBackspace(text);
            REQUIRE(text.empty());
        }
    }

    TEST_CASE("formatChatLine")
    {
        REQUIRE(formatChatLine("Player 2", "hello") == "Player 2: hello");
    }
}
