#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <rwe/mod/ModTransform.h>
#include <rwe/util/Sha256.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rwe
{
    namespace
    {
        ModUnitRecord unit(const std::string& name, double metal, double energy, double buildTime)
        {
            return {name, {{"BuildTime", buildTime}, {"EnergyCost", energy}, {"MetalCost", metal}}};
        }

        std::vector<ModUnitRecord> twoUnits()
        {
            return {
                unit("corcom", 5000, 30000, 90000),
                unit("armcom", 3000, 20000, 60000),
            };
        }

        ModTransformScript script(const std::string& source)
        {
            return {"com.example.test", "com.example.test/scripts/main.lua", source, nlohmann::json::object()};
        }
    }

    TEST_CASE("a transform scales a cost and journals the exact change", "[modsdk][modtransform]")
    {
        auto units = twoUnits();
        const auto result = runUnitTransform(script(R"(
            for _, name in ipairs(rwe.unit_names()) do
                local cost = rwe.unit_get(name, "MetalCost")
                rwe.unit_set(name, "MetalCost", cost * 1.5)
            end
        )"),
            units);

        REQUIRE(result.ok);
        REQUIRE(result.changes.size() == 2);
        CHECK(result.changes[0].unit == "armcom");
        CHECK(result.changes[0].before == 3000);
        CHECK(result.changes[0].after == 4500);
        CHECK(result.changes[1].unit == "corcom");
        CHECK(result.changes[1].before == 5000);
        CHECK(result.changes[1].after == 7500);
        // The budget is checked in 1000-instruction windows, so a short
        // script can finish inside the first window without a count.

        // The journal is ordered by unit name, and the input is now ordered too.
        CHECK(units[0].name == "armcom");
        CHECK(units[0].values.at("MetalCost") == 4500);
        CHECK(units[1].values.at("MetalCost") == 7500);
    }

    TEST_CASE("transforms compose, and each sees the previous result", "[modsdk][modtransform]")
    {
        auto units = twoUnits();
        const auto first = runUnitTransform(script(R"(
            rwe.unit_set("armcom", "MetalCost", 1000)
        )"),
            units);
        REQUIRE(first.ok);

        const auto second = runUnitTransform(script(R"(
            rwe.unit_set("armcom", "MetalCost", rwe.unit_get("armcom", "MetalCost") + 250)
        )"),
            units);
        REQUIRE(second.ok);

        REQUIRE(second.changes.size() == 1);
        CHECK(second.changes[0].before == 1000);
        CHECK(second.changes[0].after == 1250);
        CHECK(units[0].values.at("MetalCost") == 1250);
    }

    TEST_CASE("the transform sandbox has no outside world", "[modsdk][modtransform]")
    {
        auto units = twoUnits();
        const auto result = runUnitTransform(script(R"(
            assert(os == nil, "os is reachable")
            assert(io == nil, "io is reachable")
            assert(package == nil, "package is reachable")
            assert(debug == nil, "debug is reachable")
            assert(dofile == nil, "dofile is reachable")
            assert(loadfile == nil, "loadfile is reachable")
            assert(load == nil, "load is reachable")
            assert(math.random == nil, "math.random is reachable")
            assert(math.randomseed == nil, "math.randomseed is reachable")
            assert(rwe.api_version == 1)
            rwe.log("survived")
        )"),
            units);

        REQUIRE(result.ok);
        REQUIRE(result.log.size() == 1);
        CHECK(result.log[0] == "survived");
    }

    TEST_CASE("the instruction budget bounds an endless script", "[modsdk][modtransform]")
    {
        auto units = twoUnits();
        ModTransformLimits limits;
        limits.instructionBudget = 50000;
        const auto result = runUnitTransform(script("while true do end"), units, limits);

        REQUIRE_FALSE(result.ok);
        CHECK(result.error.find("instruction budget exhausted") != std::string::npos);
        CHECK(result.instructionsUsed == 50000);
    }

    TEST_CASE("a failed script restores every value it had already written", "[modsdk][modtransform]")
    {
        auto units = twoUnits();
        const auto result = runUnitTransform(script(R"(
            rwe.unit_set("armcom", "MetalCost", 1)
            error("deliberate")
        )"),
            units);

        REQUIRE_FALSE(result.ok);
        CHECK(result.error.find("deliberate") != std::string::npos);
        CHECK(result.error.find("com.example.test/scripts/main.lua") != std::string::npos);
        CHECK(result.changes.empty());
        CHECK(units[0].values.at("MetalCost") == 3000);
        CHECK(units[1].values.at("MetalCost") == 5000);
    }

    TEST_CASE("transforms round once and refuse values the engine cannot store", "[modsdk][modtransform]")
    {
        auto units = twoUnits();
        const auto rounded = runUnitTransform(script(R"(
            rwe.unit_set("armcom", "MetalCost", 3001.5)
            rwe.unit_set("armcom", "EnergyCost", 20000.4)
        )"),
            units);
        REQUIRE(rounded.ok);
        CHECK(units[0].values.at("MetalCost") == 3002);
        CHECK(units[0].values.at("EnergyCost") == 20000);

        const auto negative = runUnitTransform(script(R"(
            rwe.unit_set("armcom", "MetalCost", -1)
        )"),
            units);
        REQUIRE_FALSE(negative.ok);
        CHECK(negative.error.find("finite value") != std::string::npos);

        const auto tooBig = runUnitTransform(script(R"(
            rwe.unit_set("armcom", "MetalCost", 1e30)
        )"),
            units);
        REQUIRE_FALSE(tooBig.ok);
    }

    TEST_CASE("a transform can read its resolved options", "[modsdk][modtransform]")
    {
        auto units = twoUnits();
        auto transform = script(R"(
            local factor = rwe.options.multiplier
            rwe.unit_set("armcom", "BuildTime", rwe.unit_get("armcom", "BuildTime") * factor)
        )");
        transform.options = {{"multiplier", 2.5}};

        const auto result = runUnitTransform(transform, units);
        REQUIRE(result.ok);
        REQUIRE(result.changes.size() == 1);
        CHECK(result.changes[0].after == 150000);
    }

    TEST_CASE("the same transform input produces the same result twice", "[modsdk][modtransform]")
    {
        const auto source = R"(
            for _, name in ipairs(rwe.unit_names()) do
                rwe.unit_set(name, "EnergyCost", math.floor(rwe.unit_get(name, "EnergyCost") / 3))
            end
        )";

        auto firstUnits = twoUnits();
        auto secondUnits = twoUnits();
        std::reverse(secondUnits.begin(), secondUnits.end());

        const auto first = runUnitTransform(script(source), firstUnits);
        const auto second = runUnitTransform(script(source), secondUnits);
        REQUIRE(first.ok);
        REQUIRE(second.ok);
        REQUIRE(first.changes.size() == second.changes.size());
        for (std::size_t i = 0; i < first.changes.size(); ++i)
        {
            CHECK(first.changes[i].unit == second.changes[i].unit);
            CHECK(first.changes[i].key == second.changes[i].key);
            CHECK(first.changes[i].after == second.changes[i].after);
        }
        CHECK(first.instructionsUsed == second.instructionsUsed);
    }

    TEST_CASE("unknown units and keys fail rather than being ignored", "[modsdk][modtransform]")
    {
        auto units = twoUnits();

        const auto unknownUnit = runUnitTransform(script(R"(
            rwe.unit_set("notaunit", "MetalCost", 1)
        )"),
            units);
        REQUIRE_FALSE(unknownUnit.ok);
        CHECK(unknownUnit.error.find("unknown unit") != std::string::npos);

        const auto unknownKey = runUnitTransform(script(R"(
            rwe.unit_set("armcom", "SightDistance", 1)
        )"),
            units);
        REQUIRE_FALSE(unknownKey.ok);
        CHECK(unknownKey.error.find("no transformable key") != std::string::npos);
    }

    TEST_CASE("an oversized allocation fails inside the memory budget", "[modsdk][modtransform]")
    {
        auto units = twoUnits();
        ModTransformLimits limits;
        limits.memoryBudget = 256 * 1024;
        const auto result = runUnitTransform(script(R"(
            local chunks = {}
            for i = 1, 100 do
                chunks[i] = string.rep("x", 100000)
            end
        )"),
            units, limits);

        REQUIRE_FALSE(result.ok);
        CHECK(result.error.find("memory") != std::string::npos);
    }

    TEST_CASE("the recorded mod set keeps identity and order but not paths", "[modsdk][modtransform]")
    {
        ModLaunchContent content;
        content.identity = std::string(64, 'a');
        content.packages.push_back({"a.first", "1.0.0", std::string(64, 'b'), "a.first/scripts/main.lua",
            "C:/Downloads/a/main.lua", {{"multiplier", 1.5}}, "mutator", {}, {}, {}});
        content.packages.push_back({"b.second", "2.0.0", std::string(64, 'c'), "b.second/scripts/main.lua",
            "C:/Downloads/b/main.lua", {}, "mutator", {}, {}, std::string(64, 'e')});

        const auto json = modLaunchContentToJson(&content);
        REQUIRE_FALSE(json.is_null());
        CHECK(json["identity"] == content.identity);
        REQUIRE(json["packages"].size() == 2);
        CHECK(json["packages"][0]["id"] == "a.first");
        CHECK(json["packages"][0].contains("scriptPath") == false);

        const auto restored = modLaunchContentFromJson(json);
        REQUIRE(restored);
        CHECK(restored->identity == content.identity);
        REQUIRE(restored->packages.size() == 2);
        CHECK(restored->packages[1].id == "b.second");
        CHECK(restored->packages[1].manifestSha256 == std::string(64, 'c'));
        CHECK(restored->packages[1].entrySha256 == std::string(64, 'e'));
        CHECK(restored->packages[1].scriptPath.empty());

        CHECK(modLaunchContentToJson(nullptr).is_null());
        const auto empty = std::make_shared<ModLaunchContent>();
        CHECK(modLaunchContentToJson(empty.get()).is_null());

        REQUIRE_THROWS(modLaunchContentFromJson(nlohmann::json{{"packages", nlohmann::json::array()}}));
        REQUIRE_THROWS(modLaunchContentFromJson(nlohmann::json{{"identity", "not-a-hash"}}));
    }

    TEST_CASE("a mod set mismatch names the first difference", "[modsdk][modtransform]")
    {
        auto first = std::make_shared<ModLaunchContent>();
        first->identity = std::string(64, 'a');
        first->packages.push_back({"a.first", "1.0.0", std::string(64, 'b'), "a/scripts/main.lua", "", {}, "mutator", {}, {}, {}});
        first->packages.push_back({"b.second", "1.0.0", std::string(64, 'c'), "b/scripts/main.lua", "", {}, "mutator", {}, {}, {}});

        auto same = std::make_shared<ModLaunchContent>(*first);
        CHECK(describeModSetMismatch(first.get(), same.get()).empty());
        CHECK(describeModSetMismatch(nullptr, nullptr).empty());

        auto reordered = std::make_shared<ModLaunchContent>(*first);
        std::swap(reordered->packages[0], reordered->packages[1]);
        reordered->identity = std::string(64, 'd');
        const auto orderMessage = describeModSetMismatch(first.get(), reordered.get());
        CHECK(orderMessage.find("order differs at position 1") != std::string::npos);

        auto different = std::make_shared<ModLaunchContent>(*first);
        different->identity = std::string(64, 'e');
        different->packages[1].manifestSha256 = std::string(64, 'f');
        const auto versionMessage = describeModSetMismatch(first.get(), different.get());
        CHECK(versionMessage.find("b.second") != std::string::npos);

        // Issue #978: the same manifest with an edited script is named as a
        // script difference, not reported as the generic identity mismatch.
        auto edited = std::make_shared<ModLaunchContent>(*first);
        edited->identity = std::string(64, '9');
        edited->packages[0].entrySha256 = std::string(64, '8');
        const auto scriptMessage = describeModSetMismatch(first.get(), edited.get());
        CHECK(scriptMessage.find("a.first") != std::string::npos);
        CHECK(scriptMessage.find("different script") != std::string::npos);

        CHECK(describeModSetMismatch(first.get(), nullptr).find("without mods") != std::string::npos);
        CHECK(describeModSetMismatch(nullptr, first.get()).find("no mods") != std::string::npos);
    }

    TEST_CASE("a script is read only while its bytes match the resolved hash", "[modsdk][modtransform]")
    {
        namespace fs = std::filesystem;
        const auto path = fs::temp_directory_path() / "rwe-modtransform-verified-script.lua";
        const std::string original = "rwe.log('original')\n";
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << original;
        }

        ModLaunchPackage package;
        package.id = "a.verified";
        package.scriptName = "a.verified/scripts/main.lua";
        package.scriptPath = path.string();
        package.entrySha256 = sha256Hex(original);

        CHECK(readVerifiedModScript(package) == original);

        SECTION("an edit after resolution is refused and names the package")
        {
            {
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out << "rwe.log('edited')\n";
            }
            try
            {
                readVerifiedModScript(package);
                FAIL("an edited script was read");
            }
            catch (const std::runtime_error& e)
            {
                const std::string message = e.what();
                CHECK(message.find("a.verified") != std::string::npos);
                CHECK(message.find("changed after the mod set was resolved") != std::string::npos);
            }
        }

        SECTION("a package with no recorded hash is refused rather than trusted")
        {
            package.entrySha256.clear();
            REQUIRE_THROWS_AS(readVerifiedModScript(package), std::runtime_error);
        }

        SECTION("a missing file is refused")
        {
            package.scriptPath = (fs::temp_directory_path() / "rwe-modtransform-no-such-script.lua").string();
            REQUIRE_THROWS_AS(readVerifiedModScript(package), std::runtime_error);
        }

        std::error_code ignored;
        fs::remove(path, ignored);
    }

    TEST_CASE("format 1 exposes only the three integer cost keys", "[modsdk][modtransform]")
    {
        CHECK(isSupportedTransformKey("MetalCost"));
        CHECK(isSupportedTransformKey("EnergyCost"));
        CHECK(isSupportedTransformKey("BuildTime"));
        CHECK_FALSE(isSupportedTransformKey("SightDistance"));
        CHECK_FALSE(isSupportedTransformKey("metalcost"));
    }
}


