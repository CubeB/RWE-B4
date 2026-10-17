#include <catch2/catch_test_macros.hpp>
#include <rwe/game/chat_util.h>

namespace rwe
{
    TEST_CASE("a HUD message lasts 330 game ticks and nine show at once (BT-206)")
    {
        REQUIRE(HudMessageLifetimeTicks == 330);
        REQUIRE(HudMessageLines == 9);

        // Stamped at tick 1000 it expires at 1330 and is still up on that tick.
        const std::uint32_t expires = 1000 + HudMessageLifetimeTicks;
        REQUIRE_FALSE(hudMessageExpired(expires, 1000));
        REQUIRE_FALSE(hudMessageExpired(expires, 1329));
        REQUIRE_FALSE(hudMessageExpired(expires, 1330));
        REQUIRE(hudMessageExpired(expires, 1331));

        // The game's clock going back past a lifetime does not strand a line.
        REQUIRE(hudMessageExpired(expires, 500));
        REQUIRE_FALSE(hudMessageExpired(expires, 1000));
    }

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

    namespace
    {
        // Players 0 and 1 are one team, 2 and 3 another; 4 is on its own.
        bool testAllied(unsigned int a, unsigned int b)
        {
            auto team = [](unsigned int p) { return p / 2; };
            return team(a) == team(b);
        }

        const std::vector<unsigned int> testPeers{1, 2, 3, 4};
    }

    TEST_CASE("routeChatLine reads the one-message prefixes (BT-176)")
    {
        ChatSendMode everyone;

        SECTION("no prefix goes by the standing mode")
        {
            auto route = routeChatLine("hello there", everyone);
            REQUIRE(route.address.kind == ChatAddress::Kind::Everyone);
            REQUIRE(route.label == ChatLabel::None);
            REQUIRE(route.text == "hello there");
        }

        SECTION("A and a send to allies and label the line")
        {
            for (const char* typed : {"A, push now", "a: push now", "A; push now"})
            {
                auto route = routeChatLine(typed, everyone);
                REQUIRE(route.address.kind == ChatAddress::Kind::Allies);
                REQUIRE(route.label == ChatLabel::Allies);
                REQUIRE(route.text == "push now");
            }
        }

        SECTION("E and e send to non-allies")
        {
            auto route = routeChatLine("e, you will lose", everyone);
            REQUIRE(route.address.kind == ChatAddress::Kind::NonAllies);
            REQUIRE(route.label == ChatLabel::Enemies);
            REQUIRE(route.text == "you will lose");
        }

        SECTION("a digit sends to that slot alone and has no label")
        {
            auto route = routeChatLine("3:psst", everyone);
            REQUIRE(route.address.kind == ChatAddress::Kind::Players);
            REQUIRE(route.address.slots == (1u << 3));
            REQUIRE(route.label == ChatLabel::None);
            REQUIRE(route.text == "psst");
        }

        SECTION("leading spaces are skipped before the prefix is looked for")
        {
            auto route = routeChatLine("   A,secret", everyone);
            REQUIRE(route.address.kind == ChatAddress::Kind::Allies);
            REQUIRE(route.text == "secret");
        }

        SECTION("only a comma, colon or semicolon makes a prefix")
        {
            REQUIRE(routeChatLine("A. not a prefix", everyone).address.kind == ChatAddress::Kind::Everyone);
            REQUIRE(routeChatLine("Alpha, bravo", everyone).text == "Alpha, bravo");
            REQUIRE(routeChatLine("x, hello", everyone).text == "x, hello");
        }

        SECTION("a prefix overrides the standing mode for that line only")
        {
            ChatSendMode allies;
            allies.mode = 1;
            auto route = routeChatLine("E, hi", allies);
            REQUIRE(route.address.kind == ChatAddress::Kind::NonAllies);
            REQUIRE(routeChatLine("hi", allies).address.kind == ChatAddress::Kind::Allies);
            REQUIRE(routeChatLine("hi", allies).label == ChatLabel::Allies);
        }
    }

    TEST_CASE("ChatSendMode cycles through four modes and wraps")
    {
        ChatSendMode mode;
        mode.cycle();
        REQUIRE(mode.address().kind == ChatAddress::Kind::Allies);
        mode.cycle();
        REQUIRE(mode.address().kind == ChatAddress::Kind::NonAllies);
        mode.toggleSlot(2);
        mode.cycle();
        REQUIRE(mode.address().kind == ChatAddress::Kind::Players);
        REQUIRE(mode.address().slots == (1u << 2));
        mode.cycle();
        REQUIRE(mode.address().kind == ChatAddress::Kind::Everyone);
        mode.toggleSlot(2);
        REQUIRE(mode.customSlots == 0);
    }

    TEST_CASE("chatRecipients delivers only to the chosen peers (BT-176)")
    {
        // Local is player 0, whose team is {0, 1}.
        auto pick = [](ChatAddress a) { return chatRecipients(a, testPeers, 0, testAllied); };

        SECTION("everybody")
        {
            REQUIRE(pick({ChatAddress::Kind::Everyone, 0}) == std::vector<unsigned int>{1, 2, 3, 4});
        }

        SECTION("a line for allies never reaches an enemy")
        {
            REQUIRE(pick({ChatAddress::Kind::Allies, 0}) == std::vector<unsigned int>{1});
        }

        SECTION("a line for non-allies never reaches a teammate")
        {
            REQUIRE(pick({ChatAddress::Kind::NonAllies, 0}) == std::vector<unsigned int>{2, 3, 4});
        }

        SECTION("a custom list reaches exactly the flagged peers")
        {
            REQUIRE(pick({ChatAddress::Kind::Players, (1u << 3) | (1u << 1)}) == std::vector<unsigned int>{1, 3});
        }

        SECTION("a slot that is not a peer, or is the sender, is nobody")
        {
            REQUIRE(pick({ChatAddress::Kind::Players, (1u << 7)}).empty());
            REQUIRE(pick({ChatAddress::Kind::Players, (1u << 0)}).empty());
        }

        SECTION("no allies means an allies line goes nowhere, not to everyone")
        {
            REQUIRE(chatRecipients({ChatAddress::Kind::Allies, 0}, std::vector<unsigned int>{2, 3}, 0, testAllied).empty());
        }
    }

    TEST_CASE("capChatForWire keeps the formatted line within 64 characters")
    {
        std::string longText(200, 'x');

        SECTION("a plain line")
        {
            auto out = capChatForWire("Bob", ChatLabel::None, longText);
            // "<Bob> " is 6 characters.
            REQUIRE(out.size() == 58);
        }

        SECTION("a labelled line has less room")
        {
            auto out = capChatForWire("Bob", ChatLabel::Allies, longText);
            // "<Bob->Allies> " is 14 characters.
            REQUIRE(out.size() == 50);
        }

        SECTION("a short line is untouched")
        {
            REQUIRE(capChatForWire("Bob", ChatLabel::None, "hi") == "hi");
        }

        SECTION("a name that leaves no room sends nothing")
        {
            REQUIRE(capChatForWire(std::string(70, 'n'), ChatLabel::None, "hi").empty());
        }
    }

    TEST_CASE("formatChatLine with a label")
    {
        REQUIRE(formatChatLine("Bob", ChatLabel::Allies, "hi") == "Bob->Allies: hi");
        REQUIRE(formatChatLine("Bob", ChatLabel::Enemies, "hi") == "Bob->Enemies: hi");
        REQUIRE(formatChatLine("Bob", ChatLabel::None, "hi") == "Bob: hi");
        REQUIRE(chatLabelFromWire(1) == ChatLabel::Allies);
        REQUIRE(chatLabelFromWire(99) == ChatLabel::None);
    }
}
