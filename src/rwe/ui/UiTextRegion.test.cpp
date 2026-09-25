#include <catch2/catch_test_macros.hpp>
#include <rwe/ui/UiTextRegion.h>

namespace rwe
{
    TEST_CASE("a briefing's markup comes out, and its colours stay on the right letters", "[ui][campaign]")
    {
        auto [plain, colours] = UiTextRegion::parseMarkup("&Y*PRIORITY*& now &Rred&, &gok");
        REQUIRE(plain == "PRIORITY now red, ok");
        REQUIRE(colours.size() == plain.size());
        REQUIRE(colours[0] == 1);
        REQUIRE(colours[plain.find("now")] == 0);
        REQUIRE(colours[plain.find("red")] == 2);
        REQUIRE(colours[plain.find(",")] == 0);
        REQUIRE(colours[plain.find("ok")] == 3);
    }

    TEST_CASE("briefing text wraps between words and at its own line breaks", "[ui][campaign]")
    {
        // One unit a character, so the width is a character count.
        auto measure = [](const std::string& s) { return static_cast<float>(s.size()); };
        std::string text = "one two three\r\n\r\nfour";
        auto lines = UiTextRegion::wrap(text, 8.0f, measure);
        std::vector<std::string> got;
        for (auto [b, e] : lines)
        {
            got.push_back(text.substr(b, e - b));
        }
        REQUIRE(got == std::vector<std::string>{"one two", "three", "", "four"});

        // A word longer than the line has a line to itself.
        std::string longWord = "a enormously b";
        std::vector<std::string> gotLong;
        for (auto [b, e] : UiTextRegion::wrap(longWord, 5.0f, measure))
        {
            gotLong.push_back(longWord.substr(b, e - b));
        }
        REQUIRE(gotLong == std::vector<std::string>{"a", "enormously", "b"});
    }
}
