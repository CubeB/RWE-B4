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

    TEST_CASE("a percent suffix makes the reserve track storage", "[chat][communitypatch]")
    {
        auto c = parseChatCommand("+SetShareMetal 50%");
        REQUIRE(c->kind == Kind::SetShareMetal);
        REQUIRE(c->number == 50);
        REQUIRE(c->percent);

        c = parseChatCommand("+SetShareEnergy 50%");
        REQUIRE(c->kind == Kind::SetShareEnergy);
        REQUIRE(c->number == 50);
        REQUIRE(c->percent);

        // Without the suffix the old absolute form is unchanged.
        c = parseChatCommand("+SetShareMetal 50");
        REQUIRE(c->number == 50);
        REQUIRE_FALSE(c->percent);

        // The suffix is still a comment-safe token.
        c = parseChatCommand("+SetShareEnergy 25% # keep it");
        REQUIRE(c->number == 25);
        REQUIRE(c->percent);
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

    TEST_CASE("the render toggles take no argument", "[chat]")
    {
        REQUIRE(parseChatCommand("+AntiAlias")->kind == Kind::AntiAlias);
        REQUIRE(parseChatCommand("+Shadow")->kind == Kind::Shadow);
        REQUIRE(parseChatCommand("+Shading")->kind == Kind::Shading);
        REQUIRE(parseChatCommand("+shading 1")->kind == Kind::Shading);
        REQUIRE(parseChatCommand("+TShadow")->kind == Kind::TShadow);
        REQUIRE(parseChatCommand("+FShadow")->kind == Kind::FShadow);
        REQUIRE(parseChatCommand("+fshadow")->kind == Kind::FShadow);
    }

    TEST_CASE("the interface command reads its argument", "[chat]")
    {
        auto c = parseChatCommand("+IFace 0");
        REQUIRE(c->kind == Kind::IFace);
        REQUIRE(c->number == 0);
        REQUIRE(parseChatCommand("+iface")->number == 0);
        REQUIRE(parseChatCommand("+iface 1")->number == 1);
    }

    TEST_CASE("the stock commands read the original's argument forms", "[chat]")
    {
        auto c = parseChatCommand("+NoMetal");
        REQUIRE(c->kind == Kind::NoMetal);
        REQUIRE(c->playerNumber == -1);
        REQUIRE(c->number == 0);

        c = parseChatCommand("+NoMetal 3");
        REQUIRE(c->playerNumber == 3);
        REQUIRE(c->number == 0);

        c = parseChatCommand("+NoMetal 3 5000");
        REQUIRE(c->playerNumber == 3);
        REQUIRE(c->number == 5000);

        REQUIRE(parseChatCommand("+NoEnergy 2")->kind == Kind::NoEnergy);
        REQUIRE(parseChatCommand("+ATM")->kind == Kind::Atm);
        REQUIRE(parseChatCommand("+BPS")->kind == Kind::Bps);
        REQUIRE(parseChatCommand("+ScreenChat")->kind == Kind::ScreenChat);
        REQUIRE(parseChatCommand("+Clock")->kind == Kind::Clock);

        // `+SwitchAlt` is two forms in one word: no argument toggles, an
        // argument assigns. The parser has to keep "absent" distinct from 0.
        auto switchAlt = parseChatCommand("+SwitchAlt");
        REQUIRE(switchAlt->kind == Kind::SwitchAlt);
        REQUIRE_FALSE(switchAlt->hasArgument);
        switchAlt = parseChatCommand("+switchalt 1");
        REQUIRE(switchAlt->kind == Kind::SwitchAlt);
        REQUIRE(switchAlt->hasArgument);
        REQUIRE(switchAlt->number == 1);
        switchAlt = parseChatCommand("+SWITCHALT 0");
        REQUIRE(switchAlt->kind == Kind::SwitchAlt);
        REQUIRE(switchAlt->hasArgument);
        REQUIRE(switchAlt->number == 0);
    }

    TEST_CASE("MakePoster reads its documented forms", "[chat]")
    {
        auto c = parseChatCommand("+MakePoster");
        REQUIRE(c->kind == Kind::MakePoster);
        REQUIRE_FALSE(c->posterAll);
        REQUIRE(c->posterWidth == 0);
        REQUIRE(c->posterHeight == 0);

        c = parseChatCommand("+makeposter all");
        REQUIRE(c->kind == Kind::MakePoster);
        REQUIRE(c->posterAll);

        // The explicit two-number form is the natural reading of "each
        // dimension"; the reference does not spell out the grammar.
        c = parseChatCommand("+MakePoster 3200 2400");
        REQUIRE(c->kind == Kind::MakePoster);
        REQUIRE_FALSE(c->posterAll);
        REQUIRE(c->posterWidth == 3200);
        REQUIRE(c->posterHeight == 2400);

        // Anything else is refused rather than guessed at.
        for (const auto* line : {"+MakePoster 3200", "+MakePoster 0 100",
                 "+MakePoster -1 100", "+MakePoster 12abc 100",
                 "+MakePoster all 100", "+MakePoster 100 200 300"})
        {
            CAPTURE(line);
            REQUIRE(parseChatCommand(line)->kind == Kind::MakePosterUsage);
        }
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

    TEST_CASE("the Community Patch reclaim levels parse only with the mode on", "[chat][communitypatch]")
    {
        // TADR 2026.10.6, "Patrol reclaim thresholds". Outside the mode these
        // words are not registered at all, which is exactly what they were
        // before the mode existed: the Unknown fallback.
        REQUIRE(parseChatCommand("+SetReclaimMetal 50")->kind == Kind::Unknown);
        REQUIRE(parseChatCommand("+setreclaimenergy 50")->kind == Kind::Unknown);
        REQUIRE(parseChatCommand("+SetReclaimMetal 50", false)->kind == Kind::Unknown);

        auto metal = parseChatCommand("+SetReclaimMetal 50", true);
        REQUIRE(metal->kind == Kind::SetReclaimMetal);
        REQUIRE(metal->hasArgument);
        REQUIRE(metal->number == 50);

        // The optional percent sign is not part of the number, as for
        // +SetShareMetal; a plain number is the same level.
        REQUIRE(parseChatCommand("+setreclaimmetal 50%", true)->number == 50);
        REQUIRE(parseChatCommand("+SETRECLAIMENERGY 0%", true)->kind == Kind::SetReclaimEnergy);
        REQUIRE(parseChatCommand("+setreclaimenergy 0", true)->number == 0);
        REQUIRE(parseChatCommand("+setreclaimmetal 100", true)->number == 100);

        // No value means "print the current level", not zero: the caller has
        // to be able to tell the two apart.
        auto printing = parseChatCommand("+SetReclaimMetal", true);
        REQUIRE(printing->kind == Kind::SetReclaimMetal);
        REQUIRE_FALSE(printing->hasArgument);

        // Out-of-range values are read as typed here and clamped where applied,
        // the way the other integer commands read first and decide later.
        REQUIRE(parseChatCommand("+setreclaimmetal 200", true)->number == 200);
        REQUIRE(parseChatCommand("+setreclaimmetal -5", true)->number == -5);
    }
}
