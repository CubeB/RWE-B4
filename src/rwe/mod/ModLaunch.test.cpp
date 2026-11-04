#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <rwe/mod/ModLaunch.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        namespace fs = std::filesystem;

        fs::path exampleManifest(const std::string& example)
        {
            return fs::path(RWE_SOURCE_DIR) / "examples" / example / "rwe_mod.json";
        }

        std::string thrownMessage(const std::function<void()>& action)
        {
            try
            {
                action();
            }
            catch (const std::runtime_error& e)
            {
                return e.what();
            }
            return {};
        }
    }

    TEST_CASE("the shipped cost example resolves into the launch form", "[modsdk][modlaunch]")
    {
        const auto content = resolveModLaunchContent({exampleManifest("mod-cost-multiplier")}, {});

        CHECK(isModSha256Hex(content.identity));
        REQUIRE(content.packages.size() == 1);
        const auto& package = content.packages.front();
        CHECK(package.id == "com.example.costmultiplier");
        CHECK(package.kind == "mutator");
        CHECK(isModSha256Hex(package.entrySha256));
        CHECK(package.scriptName == "com.example.costmultiplier/scripts/main.lua");
        CHECK(fs::exists(package.scriptPath));
        CHECK(package.options.at("multiplier") == 1.5);

        // The script it names is the one the verified reader accepts.
        CHECK_FALSE(readVerifiedModScript(package).empty());

        SECTION("an option changes the identity and reaches the package")
        {
            const auto changed = resolveModLaunchContent(
                {exampleManifest("mod-cost-multiplier")},
                parseModOptionSelections({"com.example.costmultiplier:multiplier=2.5"}));
            CHECK(changed.identity != content.identity);
            CHECK(changed.packages.front().options.at("multiplier") == 2.5);
        }
    }

    TEST_CASE("a presentation example resolves without an entry hash", "[modsdk][modlaunch]")
    {
        const auto content = resolveModLaunchContent({exampleManifest("mod-presentation-hud")}, {});
        REQUIRE(content.packages.size() == 1);
        CHECK(content.packages.front().kind == "presentation");
        CHECK(content.packages.front().entrySha256.empty());
        CHECK_FALSE(content.packages.front().scriptPath.empty());
    }

    TEST_CASE("the shield example resolves as a runtime package", "[modsdk][modlaunch]")
    {
        const auto content = resolveModLaunchContent({exampleManifest("mod-shield-runtime")}, {});
        REQUIRE(content.packages.size() == 1);
        const auto& package = content.packages.front();
        CHECK(package.id == "com.example.shield");
        CHECK(isModSha256Hex(package.entrySha256));
        CHECK(std::find(package.features.begin(), package.features.end(), "runtime.unit-state@1")
            != package.features.end());
        CHECK(package.options.at("capacity") == 200);
    }

    TEST_CASE("mod option selections parse JSON values and refuse malformed ones", "[modsdk][modlaunch]")
    {
        const auto selections = parseModOptionSelections({"a.mod:n=2.5", "a.mod:b=true", "b.mod:s=hello"});
        CHECK(selections.at("a.mod").at("n") == 2.5);
        CHECK(selections.at("a.mod").at("b") == true);
        CHECK(selections.at("b.mod").at("s") == "hello");

        for (const std::string bad : {"a.mod=1", ":k=1", "a.mod:=1", "a.mod:k=", "a.mod:k"})
        {
            INFO(bad);
            CHECK_THROWS_AS(parseModOptionSelections({bad}), std::runtime_error);
        }
    }

    TEST_CASE("--mod arguments start a set or match a recorded one", "[modsdk][modlaunch]")
    {
        const std::vector<std::string> manifests = {exampleManifest("mod-cost-multiplier").string()};
        const auto recorded =
            std::make_shared<const ModLaunchContent>(resolveModLaunchContent({exampleManifest("mod-cost-multiplier")}, {}));

        SECTION("no arguments leave the current set alone")
        {
            CHECK(applyModArguments({}, {}, recorded, true) == recorded);
            CHECK(applyModArguments({}, {}, nullptr, false) == nullptr);
        }

        SECTION("an option without a package is refused")
        {
            CHECK(thrownMessage([&] { applyModArguments({}, {"a:k=1"}, nullptr, false); }).find("--mod")
                != std::string::npos);
        }

        SECTION("a fresh game takes the resolved set")
        {
            const auto content = applyModArguments(manifests, {}, nullptr, false);
            REQUIRE(content);
            CHECK(content->identity == recorded->identity);
        }

        SECTION("a recording's own set is accepted with this machine's paths")
        {
            // What a save or replay holds: identity and fingerprints, no paths.
            auto withoutPaths = std::make_shared<ModLaunchContent>(*recorded);
            for (auto& package : withoutPaths->packages)
            {
                package.scriptPath.clear();
            }
            const auto content = applyModArguments(manifests, {}, withoutPaths, true);
            REQUIRE(content);
            CHECK_FALSE(content->packages.front().scriptPath.empty());
        }

        SECTION("a different set is refused, naming the difference")
        {
            const auto message = thrownMessage([&] {
                applyModArguments(
                    manifests, {"com.example.costmultiplier:multiplier=3"}, recorded, true);
            });
            CHECK(message.find("does not match the recording") != std::string::npos);
        }

        SECTION("mods against a recording without mods are refused")
        {
            const auto message = thrownMessage([&] { applyModArguments(manifests, {}, nullptr, true); });
            CHECK(message.find("without mods") != std::string::npos);
        }
    }

    TEST_CASE("a manifest naming a missing entry script is refused", "[modsdk][modlaunch]")
    {
        const auto root = fs::temp_directory_path() / "rwe-modlaunch-missing-entry";
        fs::create_directories(root);
        const auto manifest = root / "rwe_mod.json";
        {
            std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
            out << R"({"manifestVersion":1,"id":"a.missing","name":"a","version":"1.0.0",)"
                << R"("kind":"mutator","api":1,"entry":"scripts/main.lua"})";
        }
        const auto message = thrownMessage([&] { readModPackageFile(manifest); });
        CHECK(message.find("a.missing") != std::string::npos);
        CHECK(message.find("missing") != std::string::npos);

        std::error_code ignored;
        fs::remove_all(root, ignored);
    }

    TEST_CASE("package resource directories apply to the path mapping", "[modsdk][modlaunch]")
    {
        ModLaunchContent content;
        ModLaunchPackage package;
        package.id = "a.prota";
        package.resourceDirectories = {{"weapons", "weaponP"}};
        content.packages.push_back(package);

        PathMapping mapping;
        mapping.weapons = "weapons";
        applyModResourceDirectories(mapping, &content);
        CHECK(mapping.weapons == "weaponP");

        applyModResourceDirectories(mapping, nullptr);
        CHECK(mapping.weapons == "weaponP");

        content.packages[0].resourceDirectories = {{"nonsense", "x"}};
        CHECK(thrownMessage([&] { applyModResourceDirectories(mapping, &content); }).find("a.prota")
            != std::string::npos);
    }
}
