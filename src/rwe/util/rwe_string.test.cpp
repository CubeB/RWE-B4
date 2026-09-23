#include <catch2/catch_test_macros.hpp>

#include <rwe/util/rwe_string.h>

namespace rwe
{
    TEST_CASE("utf8Split")
    {
        SECTION("works on empty string")
        {
            std::string s;
            std::vector<std::string> expected{""};

            auto actual = utf8Split(s, '/');

            REQUIRE(actual == expected);
        }

        SECTION("splits a utf8 string on a delimiter")
        {
            std::string s("foo/bar/baz");
            std::vector<std::string> expected{"foo", "bar", "baz"};

            auto actual = utf8Split(s, '/');

            REQUIRE(actual == expected);
        }

        SECTION("works when the delimiter is not found")
        {
            std::string s("foo.bar.baz");
            std::vector<std::string> expected{"foo.bar.baz"};

            auto actual = utf8Split(s, '/');

            REQUIRE(actual == expected);
        }

        SECTION("gives back the bytes a multi-byte code point arrived as")
        {
            // The point of walking code points is not to cut one in half; it
            // is no better to put one back together wrongly. U+00E9 is two
            // bytes in and must be two bytes out, not the single byte 0xE9
            // that narrowing a code point to a char leaves behind -- which is
            // latin1 again, and throws the next time anything walks it.
            std::string s("Jos\xC3\xA9;Computer;ARM;0");

            auto actual = utf8Split(s, ';');

            REQUIRE(actual.size() == 4);
            REQUIRE(actual[0] == std::string("Jos\xC3\xA9"));
            REQUIRE(actual[0] == ensureUtf8(actual[0]));
        }

        SECTION("carries a code point that is three bytes long across intact")
        {
            std::string s("a;\xC3\xBB\xE2\x82\xAC;z");
            std::vector<std::string> expected{"a", "\xC3\xBB\xE2\x82\xAC", "z"};

            auto actual = utf8Split(s, ';');

            REQUIRE(actual == expected);
        }
    }

    TEST_CASE("split")
    {
        SECTION("works on empty string")
        {
            std::string s;
            std::vector<std::string> expected{""};

            auto actual = split(s, '/');

            REQUIRE(actual == expected);
        }

        SECTION("splits a utf8 string on a delimiter")
        {
            std::string s("foo/bar/baz");
            std::vector<std::string> expected{"foo", "bar", "baz"};

            auto actual = split(s, '/');

            REQUIRE(actual == expected);
        }

        SECTION("works when the delimiter is not found")
        {
            std::string s("foo.bar.baz");
            std::vector<std::string> expected{"foo.bar.baz"};

            auto actual = split(s, '/');

            REQUIRE(actual == expected);
        }
    }

    TEST_CASE("toUpper")
    {
        SECTION("converts a string to uppercase")
        {
            REQUIRE(toUpper(std::string("Foo bAr baZ")) == std::string("FOO BAR BAZ"));
        }
    }

    TEST_CASE("utf8TrimChecked")
    {
        SECTION("trims leading and trailing spaces")
        {
            std::string s("    foo  ");
            utf8Trim(s);
            REQUIRE(s == "foo");
        }

        SECTION("leaves a code point no byte of which is ASCII alone")
        {
            // U+4E00 does not fit an unsigned char, and isspace is not
            // defined for a value that does not. See isSpaceCodePoint.
            std::string s("  \xE4\xB8\x80  ");
            utf8Trim(s);
            REQUIRE(s == std::string("\xE4\xB8\x80"));
        }
    }

    TEST_CASE("utf8TrimUnchecked")
    {
        SECTION("trims leading and trailing spaces")
        {
            std::string s("    foo  ");
            utf8UncheckedTrim(s);
            REQUIRE(s == "foo");
        }

        SECTION("leaves a code point no byte of which is ASCII alone")
        {
            std::string s("  \xE4\xB8\x80  ");
            utf8UncheckedTrim(s);
            REQUIRE(s == std::string("\xE4\xB8\x80"));
        }
    }

    TEST_CASE("endsWith")
    {
        SECTION("returns true if the string ends with the substring")
        {
            std::string s("foo.txt");
            std::string e(".txt");
            REQUIRE(endsWith(s, e));
        }

        SECTION("returns false otherwise")
        {
            std::string s("foottxt");
            std::string e(".txt");
            REQUIRE(!endsWith(s, e));
        }

        SECTION("returns false when the string is shorter")
        {
            std::string s("txt");
            std::string e(".txt");
            REQUIRE(!endsWith(s, e));
        }
    }

    TEST_CASE("endsWithUtf8")
    {
        SECTION("returns true if the string ends with the substring")
        {
            std::string s("foo.txt");
            std::string e(".txt");
            REQUIRE(endsWithUtf8(s, e));
        }

        SECTION("returns false otherwise")
        {
            std::string s("foottxt");
            std::string e(".txt");
            REQUIRE(!endsWithUtf8(s, e));
        }

        SECTION("returns false when the string is shorter")
        {
            std::string s("txt");
            std::string e(".txt");
            REQUIRE(!endsWithUtf8(s, e));
        }
    }

    TEST_CASE("startsWith")
    {
        SECTION("returns true if the string starts with the substring")
        {
            std::string s("foo.txt");
            std::string p("foo");
            REQUIRE(startsWith(s, p));
        }

        SECTION("returns false otherwise")
        {
            std::string s("fobar.txt");
            std::string p("foo");
            REQUIRE(!startsWith(s, p));
        }

        SECTION("returns false when the string is shorter")
        {
            std::string s("foo");
            std::string p("fooo");
            REQUIRE(!startsWith(s, p));
        }
    }

    TEST_CASE("utf8SplitLast")
    {
        SECTION("splits the last occurrence of a character")
        {
            REQUIRE(utf8SplitLast("foo.bar.baz", U'.').value() == std::make_pair(std::string("foo.bar"), std::string("baz")));
            REQUIRE(!utf8SplitLast("foo:bar:baz", U'.'));
        }
    }

    TEST_CASE("ensureUtf8")
    {
        SECTION("leaves valid UTF-8 exactly as it was")
        {
            // Byte for byte: a name that is already UTF-8 is also a name used
            // to open a file, and a transcode would break the lookup.
            REQUIRE(ensureUtf8("Coast To Coast") == "Coast To Coast");
            REQUIRE(ensureUtf8("Ca\xc3\xb1\xc3\xb3n") == "Ca\xc3\xb1\xc3\xb3n");
            REQUIRE(ensureUtf8("") == "");
        }

        SECTION("reads what is not UTF-8 as latin1, keeping the byte in the code point")
        {
            // 0xE9 alone is not a UTF-8 sequence. As latin1 it is U+00E9,
            // which encodes as C3 A9 -- and the original 0xE9 is still in the
            // code point, for anything that later knows better.
            REQUIRE(ensureUtf8("Caf\xe9") == "Caf\xc3\xa9");
            REQUIRE(ensureUtf8("\xff") == "\xc3\xbf");
        }

        SECTION("gives back something the interface can walk without throwing")
        {
            // Which is the whole point: every string drawn is walked with a
            // checked iterator, and a throw inside a draw call takes the game
            // down rather than losing a caption.
            auto safe = ensureUtf8("map \xe9 \xff \x80");
            REQUIRE(utf8::is_valid(safe.begin(), safe.end()));

            std::vector<unsigned int> codePoints;
            for (auto it = cUtf8Begin(safe); it != cUtf8End(safe); ++it)
            {
                codePoints.push_back(*it);
            }
            REQUIRE(codePoints.size() == 9);
            REQUIRE(codePoints[4] == 0xe9);
            REQUIRE(codePoints[6] == 0xff);
            REQUIRE(codePoints[8] == 0x80);
        }

        SECTION("is not fooled by a sequence cut short at the end")
        {
            // A name truncated mid-character is what an unchecked walk runs
            // off the end of.
            auto safe = ensureUtf8("nearly\xc3");
            REQUIRE(utf8::is_valid(safe.begin(), safe.end()));
        }
    }
}
