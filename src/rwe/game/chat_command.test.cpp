#include <catch2/catch_test_macros.hpp>
#include <climits>
#include <rwe/game/chat_command.h>

/**
 * How a `+` line is read (TOTALA-EXE-COMMANDS.md section 124): whitespace
 * tokens, `#` to end of line, the command word without regard to case, and an
 * integer argument read as `atoi` reads it.
 */
namespace rwe
{
    using Kind = ChatCommand::Kind;

    TEST_CASE("ordinary chat is not a command", "[chat]")
    {
        REQUIRE_FALSE(parseChatCommand("hello there").has_value());
        REQUIRE_FALSE(parseChatCommand("").has_value());
        REQUIRE_FALSE(parseChatCommand("   ").has_value());
        // A plus that is not the first character is just a plus.
        REQUIRE_FALSE(parseChatCommand("1 + 1 = 2").has_value());
        REQUIRE_FALSE(parseChatCommand("#+ShareMetal").has_value());
    }

    TEST_CASE("a line whose first non-space character is + is a command", "[chat]")
    {
        REQUIRE(parseChatCommand("+ShareMetal")->kind == Kind::ShareMetal);
        REQUIRE(parseChatCommand("   +ShareMetal")->kind == Kind::ShareMetal);
        REQUIRE(parseChatCommand("\t+ShareMetal")->kind == Kind::ShareMetal);
    }

    TEST_CASE("command words are matched without regard to case", "[chat]")
    {
        REQUIRE(parseChatCommand("+SHAREMETAL")->kind == Kind::ShareMetal);
        REQUIRE(parseChatCommand("+sharemetal")->kind == Kind::ShareMetal);
        REQUIRE(parseChatCommand("+ShArEeNeRgY")->kind == Kind::ShareEnergy);
        REQUIRE(parseChatCommand("+shareall")->kind == Kind::ShareAll);
        REQUIRE(parseChatCommand("+SHAREMAPPING")->kind == Kind::ShareMapping);
        REQUIRE(parseChatCommand("+shareradar")->kind == Kind::ShareRadar);
    }

    TEST_CASE("the word is what follows the plus, whitespace or not", "[chat]")
    {
        REQUIRE(parseChatCommand("+ ShareEnergy")->kind == Kind::ShareEnergy);
        REQUIRE(parseChatCommand("+   ShareEnergy   ")->kind == Kind::ShareEnergy);
    }

    TEST_CASE("a toggle takes no argument and ignores one", "[chat]")
    {
        // "+ShareEnergy off" flips it like "+ShareEnergy".
        REQUIRE(parseChatCommand("+ShareEnergy off")->kind == Kind::ShareEnergy);
        REQUIRE(parseChatCommand("+ShareMetal 1 2 3")->kind == Kind::ShareMetal);
    }

    TEST_CASE("a lone plus is nothing to run", "[chat]")
    {
        REQUIRE(parseChatCommand("+")->kind == Kind::Empty);
        REQUIRE(parseChatCommand("  +   ")->kind == Kind::Empty);
        REQUIRE(parseChatCommand("+ # nothing here")->kind == Kind::Empty);
    }

    TEST_CASE("# starts a comment, even inside a word", "[chat]")
    {
        auto c = parseChatCommand("+SetShareMetal 300 # keep it");
        REQUIRE(c->kind == Kind::SetShareMetal);
        REQUIRE(c->number == 300);

        // Nothing after the # is read, so the argument is missing.
        c = parseChatCommand("+SetShareMetal # 300");
        REQUIRE(c->kind == Kind::SetShareMetal);
        REQUIRE(c->number == 0);

        // The comment cuts the word itself.
        c = parseChatCommand("+SetShareMetal 12#34");
        REQUIRE(c->number == 12);
        c = parseChatCommand("+ShareMetal#comment");
        REQUIRE(c->kind == Kind::ShareMetal);
    }

    TEST_CASE("an integer argument is read the way atoi reads it", "[chat]")
    {
        auto number = [](const std::string& line) { return parseChatCommand(line)->number; };

        REQUIRE(number("+SetShareEnergy 500") == 500);
        REQUIRE(number("+SetShareEnergy    500") == 500);
        REQUIRE(number("+SetShareEnergy 12abc") == 12);
        REQUIRE(number("+SetShareEnergy abc") == 0);
        REQUIRE(number("+SetShareEnergy abc 40") == 0);
        REQUIRE(number("+SetShareEnergy") == 0);
        REQUIRE(number("+SetShareEnergy -75") == -75);
        REQUIRE(number("+SetShareEnergy +75") == 75);
        REQUIRE(number("+SetShareEnergy -") == 0);
        REQUIRE(number("+SetShareEnergy 3.9") == 3);
        REQUIRE(number("+SetShareEnergy 0x10") == 0);

        // Out of range saturates instead of being undefined.
        REQUIRE(number("+SetShareEnergy 99999999999999999999") == INT_MAX);
        REQUIRE(number("+SetShareEnergy -99999999999999999999") == INT_MIN);
        REQUIRE(number("+SetShareEnergy 2147483647") == INT_MAX);
        REQUIRE(number("+SetShareEnergy -2147483648") == INT_MIN);
    }

    TEST_CASE("the two reserve setters read their own resource", "[chat]")
    {
        REQUIRE(parseChatCommand("+SetShareMetal 10")->kind == Kind::SetShareMetal);
        REQUIRE(parseChatCommand("+SetShareEnergy 10")->kind == Kind::SetShareEnergy);
    }

    TEST_CASE("a gift names a player, an amount and a resource", "[chat]")
    {
        auto c = parseChatCommand("+Give 2 150 metal");
        REQUIRE(c->kind == Kind::Give);
        REQUIRE(c->playerToken == "2");
        REQUIRE(c->number == 150);
        REQUIRE(c->giveMetal);

        c = parseChatCommand("+give Bob 20abc ENERGY");
        REQUIRE(c->kind == Kind::Give);
        REQUIRE(c->playerToken == "Bob");
        REQUIRE(c->number == 20);
        REQUIRE_FALSE(c->giveMetal);

        c = parseChatCommand("+Give 1 -50 metal");
        REQUIRE(c->kind == Kind::Give);
        REQUIRE(c->number == -50);
    }

    TEST_CASE("a gift without all its arguments is a usage message, not a guess", "[chat]")
    {
        REQUIRE(parseChatCommand("+Give")->kind == Kind::GiveUsage);
        REQUIRE(parseChatCommand("+Give 2")->kind == Kind::GiveUsage);
        REQUIRE(parseChatCommand("+Give 2 100")->kind == Kind::GiveUsage);
        REQUIRE(parseChatCommand("+Give 2 100 tin")->kind == Kind::GiveUsage);
    }

    TEST_CASE("the developer phrase is exactly six tokens and five of them are case-sensitive", "[chat]")
    {
        auto c = parseChatCommand("+Now Film Chris Include Reload Assert");
        REQUIRE(c->kind == Kind::Now);
        REQUIRE(c->developerPhraseAccepted);

        // The command word is matched case-insensitively.
        REQUIRE(parseChatCommand("+nOw Film Chris Include Reload Assert")->developerPhraseAccepted);

        SECTION("anything else clears the unlock rather than failing to parse")
        {
            for (const auto* line : {"+Now", "+Now Film Chris Include Reload",
                     "+Now film Chris Include Reload Assert",
                     "+Now Film Chris Include Reload Assert extra",
                     "+Now Film Chris Include Reload Asserted"})
            {
                CAPTURE(line);
                auto parsed = parseChatCommand(line);
                REQUIRE(parsed->kind == Kind::Now);
                REQUIRE_FALSE(parsed->developerPhraseAccepted);
            }
        }
    }

    TEST_CASE("the permission mask follows the original's four cases", "[chat]")
    {
        // #906: campaign 1; skirmish 3; multiplayer the host's Cheat Codes
        // option; the local developer phrase raises any of them to 7.
        REQUIRE(chatCommandMask(false, true, false, false) == ChatCommandMaskOrdinary);
        REQUIRE(chatCommandMask(false, false, false, false) == (ChatCommandMaskOrdinary | ChatCommandMaskCheat));
        REQUIRE(chatCommandMask(false, false, true, false) == ChatCommandMaskOrdinary);
        REQUIRE(chatCommandMask(false, false, true, true) == (ChatCommandMaskOrdinary | ChatCommandMaskCheat));
        REQUIRE(chatCommandMask(true, true, false, false) == 7u);
        REQUIRE(chatCommandMask(true, false, true, false) == 7u);
    }

    TEST_CASE("the local display toggles parse without arguments", "[chat]")
    {
        REQUIRE(parseChatCommand("+NoShake")->kind == Kind::NoShake);
        REQUIRE(parseChatCommand("+noshake 1")->kind == Kind::NoShake);
    }

    TEST_CASE("the option setters read argument one and default it to zero", "[chat]")
    {
        auto c = parseChatCommand("+Gamma 12");
        REQUIRE(c->kind == Kind::Gamma);
        REQUIRE(c->number == 12);
        REQUIRE(parseChatCommand("+Gamma")->number == 0);

        c = parseChatCommand("+ScrollSpeed 32");
        REQUIRE(c->kind == Kind::ScrollSpeed);
        REQUIRE(c->number == 32);
        REQUIRE(parseChatCommand("+scrollspeed")->number == 0);
    }

    TEST_CASE("the audio commands parse their own arguments", "[chat]")
    {
        auto c = parseChatCommand("+MusicMode 1");
        REQUIRE(c->kind == Kind::MusicMode);
        REQUIRE(c->number == 1);
        REQUIRE(parseChatCommand("+musicmode")->number == 0);

        REQUIRE(parseChatCommand("+Sing")->kind == Kind::Sing);
    }

    TEST_CASE("every other word is unknown, and says what it was", "[chat]")
    {
        auto c = parseChatCommand("+Contour 4");
        REQUIRE(c->kind == Kind::Unknown);
        REQUIRE(c->word == "Contour");

        // The original does not register a line-of-sight toggle either.
        c = parseChatCommand("+ShareLOS");
        REQUIRE(c->kind == Kind::Unknown);
        REQUIRE(c->word == "ShareLOS");

        // Not a prefix match.
        REQUIRE(parseChatCommand("+Share")->kind == Kind::Unknown);
        REQUIRE(parseChatCommand("+ShareMetals")->kind == Kind::Unknown);
    }

    TEST_CASE("words past the token cap are dropped, not an error", "[chat]")
    {
        // Padding after a command's own arguments changes nothing.
        std::string line = "+Give 2 100 metal";
        for (int i = 0; i < 40; ++i)
        {
            line += " x";
        }
        REQUIRE(parseChatCommand(line)->kind == Kind::Give);
    }

    TEST_CASE("+ShareMapping and +ShareRadar no longer claim allies share radar", "[chat]")
    {
        auto radar = shareNoticeMessage(Kind::ShareRadar);
        REQUIRE(radar.find("already share") == std::string::npos);
        REQUIRE(radar.find("not shared with allies") != std::string::npos);

        auto mapping = shareNoticeMessage(Kind::ShareMapping);
        REQUIRE(mapping.find("already share") == std::string::npos);
        REQUIRE(mapping.find("always share what they see") != std::string::npos);
    }
}
