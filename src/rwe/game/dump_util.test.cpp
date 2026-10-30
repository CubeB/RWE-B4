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

    TEST_CASE("the explored grid is dumped as runs, a string a row")
    {
        SECTION("an unexplored map is one short line a row")
        {
            Grid<ExploredMask> grid(6, 2, ExploredMask(0));
            const auto j = dumpJson(grid);
            CHECK(j["width"] == 6);
            CHECK(j["height"] == 2);
            REQUIRE(j["rows"].size() == 2);
            CHECK(j["rows"][0] == "0*6");
            CHECK(j["rows"][1] == "0*6");
        }

        SECTION("a run stops where the value changes, and a row stops where the row does")
        {
            Grid<ExploredMask> grid(5, 2, ExploredMask(0));
            grid.set(1, 0, ExploredMask(3));
            grid.set(2, 0, ExploredMask(3));
            grid.set(4, 0, ExploredMask(1));
            // the second row starts with the value the first ended on
            grid.set(0, 1, ExploredMask(1));
            const auto j = dumpJson(grid);
            CHECK(j["rows"][0] == "0*1 3*2 0*1 1*1");
            CHECK(j["rows"][1] == "1*1 0*4");
        }

        SECTION("a degenerate grid has no rows")
        {
            Grid<ExploredMask> grid(0, 0, ExploredMask(0));
            CHECK(dumpJson(grid)["rows"].empty());
        }
    }

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
     * An omitted walk is a named, reasoned omission -- the variant shape
     * makes the illegal combinations unrepresentable, but a blank reason
     * string is still writable, and one of those teaches a reader who meets
     * it nothing about why the field sits out. This names the row.
     */
    TEST_CASE("every omission in the field table names its reason", "[dump]")
    {
        std::vector<std::string> unnamed;
        for (const auto& field : unitStateFieldTable())
        {
            std::visit(
                [&](const auto& w)
                {
                    using T = std::decay_t<decltype(w)>;
                    if constexpr (std::is_same_v<T, UnitStateFieldUnhashed>)
                    {
                        if (w.whyNotHashed == nullptr || w.whyNotHashed[0] == '\0')
                        {
                            unnamed.emplace_back(field.name);
                        }
                    }
                    else if constexpr (std::is_same_v<T, UnitStateFieldSaveOnly>)
                    {
                        if (w.whyNotHashed == nullptr || w.whyNotHashed[0] == '\0' || w.whyNotLoaded == nullptr || w.whyNotLoaded[0] == '\0')
                        {
                            unnamed.emplace_back(field.name);
                        }
                    }
                },
                field.walks);
        }

        CAPTURE(unnamed);
        REQUIRE(unnamed.empty());
    }

    TEST_CASE("a player's sharing flags and reserves reach the desync dump", "[dump]")
    {
        // They are hashed (resource sharing, TOTALA-EXE-COMMANDS.md section
        // 124), so a report that did not carry them could not say which of
        // them two peers disagreed about.
        GamePlayerInfo p{
            std::optional<std::string>("p"),
            GamePlayerType::Human,
            PlayerColorIndex(0),
            GamePlayerStatus::Alive,
            std::string("ARM"),
            Metal(100.0f),
            Energy(100.0f),
            Metal(1000.0f),
            Energy(1000.0f),
            Metal(100.0f),
            Energy(100.0f),
        };
        p.shareMetal = true;
        p.shareEnergy = false;
        p.metalReserve = Metal(250.0f);
        p.energyReserve = Energy(-5.0f);
        p.reclaimMetalPercent = 7;
        p.reclaimEnergyPercent = 93;

        auto j = dumpJson(p);
        REQUIRE(j.at("shareMetal") == true);
        REQUIRE(j.at("shareEnergy") == false);
        REQUIRE(j.at("metalReserve") == 250.0f);
        REQUIRE(j.at("energyReserve") == -5.0f);
        REQUIRE(j.at("reclaimMetalPercent") == 7);
        REQUIRE(j.at("reclaimEnergyPercent") == 93);
    }

}
