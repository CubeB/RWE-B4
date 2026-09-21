#include <catch2/catch_test_macros.hpp>
#include <rwe/game/UnitStateFieldTable.h>
#include <rwe/game/dump_util.h>

namespace rwe
{
    struct IdTag;
    using Id = OpaqueId<unsigned int, IdTag>;

    enum class TestEnum
    {
        CaseA,
        CaseB,
        CaseC
    };

    using TestVariant = std::variant<int, bool, float>;

    TEST_CASE("dumpJson")
    {
        SECTION("works on float")
        {
            REQUIRE(dumpJson(5.5f) == "5.5"_json);
        }
        SECTION("works on bool")
        {
            REQUIRE(dumpJson(true) == "true"_json);
        }
        SECTION("works on unsigned int")
        {
            REQUIRE(dumpJson(1234u) == "1234"_json);
        }
        SECTION("works on int")
        {
            REQUIRE(dumpJson(1234) == "1234"_json);
            REQUIRE(dumpJson(-50) == "-50"_json);
        }
        SECTION("works on string")
        {
            REQUIRE(dumpJson(std::string("A")) == "\"A\""_json);
        }
        SECTION("works on optional")
        {
            REQUIRE(dumpJson(std::make_optional(38)) == "38"_json);
            REQUIRE(dumpJson(std::optional<int>()) == "null"_json);
        }
        SECTION("works on vector")
        {
            std::vector<int> v{1, 2, 3, 4};
            REQUIRE(dumpJson(v) == "[1, 2, 3, 4]"_json);
        }
        SECTION("works on pair")
        {
            std::pair<int, int> v{1, 2};
            REQUIRE(dumpJson(v) == R"({ "first": 1, "second": 2 })"_json);
        }
        SECTION("works on VectorMap")
        {
            VectorMap<int, IdTag> v;
            v.emplace(1);
            v.emplace(2);
            v.emplace(3);
            REQUIRE(dumpJson(v) == R"([{ "first": 0, "second": 1 }, { "first": 256, "second": 2 }, { "first": 512, "second": 3 }])"_json);
        }
        SECTION("works on enum")
        {
            REQUIRE(dumpJson(TestEnum::CaseA) == "0"_json);
            REQUIRE(dumpJson(TestEnum::CaseB) == "1"_json);
            REQUIRE(dumpJson(TestEnum::CaseC) == "2"_json);
        }
        SECTION("works on variant")
        {
            REQUIRE(dumpJson(TestVariant(12)) == R"({ "variant": 0, "data": 12 })"_json);
            REQUIRE(dumpJson(TestVariant(true)) == R"({ "variant": 1, "data": true })"_json);
            REQUIRE(dumpJson(TestVariant(1.0f)) == R"({ "variant": 2, "data": 1 })"_json);
        }
    }

    /**
     * The rule the field table exists to keep: a field the sync hash reads
     * and the dump does not is a desync nobody can bisect. The documented
     * hunt is RWE_HASH_LOG to find the tick, then RWE_STATE_DUMP to find the
     * field -- and the second step can only show what the dump walks.
     *
     * This is issue #115's "nowhere left to hide", as far as a test can carry
     * it. A null dump step beside a hash step is legal to the compiler, so
     * nothing but this case will notice one.
     */
    TEST_CASE("every field the hash reads is one the dump writes", "[dump]")
    {
        std::vector<std::string> hashedButNotDumped;
        for (const auto& field : unitStateFieldTable())
        {
            if (field.hash != nullptr && field.dump == nullptr)
            {
                hashedButNotDumped.emplace_back(field.name);
            }
        }

        CAPTURE(hashedButNotDumped);
        REQUIRE(hashedButNotDumped.empty());
    }

}
