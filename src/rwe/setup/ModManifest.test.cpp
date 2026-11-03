#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <rwe/setup/ModManifest.h>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace rwe
{
    namespace
    {
        json manifestJson(const std::string& id, const std::string& version = "1.0.0")
        {
            return {
                {"manifestVersion", 1},
                {"id", id},
                {"name", id},
                {"version", version},
                {"kind", "content"},
                {"api", 1},
            };
        }

        ModPackage packageFrom(const json& value, const std::string& source = "fixture")
        {
            const auto result = modPackageFromJson(value, value.dump(), source);
            REQUIRE(result);
            return result.get();
        }

        ModResolutionEnvironment environmentWithFeatures(std::vector<std::string> features)
        {
            ModResolutionEnvironment environment;
            environment.supportedFeatures = std::move(features);
            return environment;
        }

        Result<ResolvedModSet, ModManifestError> resolve(
            std::vector<ModPackage> packages,
            ModOptionSelections selections = {},
            ModResolutionEnvironment environment = {})
        {
            return resolveModSet(std::move(packages), selections, environment);
        }
    }

    TEST_CASE("sha256 of the published test vectors", "[setup][modsdk]")
    {
        CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
            == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
        CHECK(sha256Hex(std::string(1000000, 'a'))
            == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }

    TEST_CASE("versions are major.minor.patch and constraints are evaluated", "[setup][modsdk]")
    {
        const auto version = parseModVersion("1.2.3");
        REQUIRE(version);
        CHECK(version.get() == ModVersion{1, 2, 3});
        CHECK(modVersionText(version.get()) == "1.2.3");

        CHECK_FALSE(parseModVersion("1.2"));
        CHECK_FALSE(parseModVersion("1.2.3.4"));
        CHECK_FALSE(parseModVersion("01.2.3"));
        CHECK_FALSE(parseModVersion("1.2.3-beta"));
        CHECK_FALSE(parseModVersion("1.2.3+build"));

        const auto check = [](const char* text, const char* constraint, bool expected) {
            const auto parsedVersion = parseModVersion(text);
            REQUIRE(parsedVersion);
            const auto satisfied = satisfiesModConstraint(parsedVersion.get(), constraint);
            REQUIRE(satisfied);
            CHECK(satisfied.get() == expected);
        };

        check("1.4.0", "^1.2.3", true);
        check("2.0.0", "^1.2.3", false);
        check("0.2.9", "^0.2.3", true);
        check("0.3.0", "^0.2.3", false);
        check("1.2.9", "~1.2.3", true);
        check("1.3.0", "~1.2.3", false);
        check("1.9.9", ">=1.2.0 <2.0.0", true);
        check("2.0.0", ">=1.2.0 <2.0.0", false);
        check("9.9.9", "*", true);
        check("1.2.3", "1.2.3", true);

        CHECK_FALSE(satisfiesModConstraint(ModVersion{1, 2, 3}, ""));
        CHECK_FALSE(satisfiesModConstraint(ModVersion{1, 2, 3}, ">= "));
        const auto repeated = satisfiesModConstraint(ModVersion{1, 2, 3}, "1.2.3 1.2.3");
        REQUIRE(repeated);
        CHECK(repeated.get());
    }

    TEST_CASE("a format 1 manifest needs identity, version and a valid entry", "[setup][modsdk]")
    {
        const auto parsed = parseModManifest(manifestJson("com.example.cost"), "fixture");
        REQUIRE(parsed);
        CHECK(parsed.get().id == "com.example.cost");
        CHECK(parsed.get().version == ModVersion{1, 0, 0});
        CHECK(parsed.get().kind == ModKind::Content);
        CHECK_FALSE(parsed.get().legacy);

        SECTION("a missing id is named")
        {
            auto value = manifestJson("x");
            value.erase("id");
            const auto result = parseModManifest(value, "fixture");
            REQUIRE_FALSE(result);
            CHECK(result.getErr().code == ModErrorCode::MissingField);
            CHECK(result.getErr().describe().find("id") != std::string::npos);
        }

        SECTION("a newer manifest format is refused, not guessed")
        {
            auto value = manifestJson("x");
            value["manifestVersion"] = 2;
            const auto result = parseModManifest(value, "fixture");
            REQUIRE_FALSE(result);
            CHECK(result.getErr().code == ModErrorCode::UnsupportedManifestVersion);
        }

        SECTION("a mutator needs a .lua entry and a normalized path")
        {
            auto value = manifestJson("x");
            value["kind"] = "mutator";
            auto result = parseModManifest(value, "fixture");
            REQUIRE_FALSE(result);
            CHECK(result.getErr().code == ModErrorCode::MissingField);

            value["entry"] = "scripts/../main.lua";
            result = parseModManifest(value, "fixture");
            REQUIRE_FALSE(result);
            CHECK(result.getErr().code == ModErrorCode::InvalidField);

            value["entry"] = "scripts/main.lua";
            result = parseModManifest(value, "fixture");
            REQUIRE(result);
            CHECK(result.get().entry == "scripts/main.lua");
        }

        SECTION("replaces must name a key the package claims")
        {
            auto value = manifestJson("x");
            value["replaces"] = json::array({"other:hook.damage.v1"});
            const auto result = parseModManifest(value, "fixture");
            REQUIRE_FALSE(result);
            CHECK(result.getErr().code == ModErrorCode::InvalidField);
            CHECK(result.getErr().describe().find("hook.damage.v1") != std::string::npos);
        }

        SECTION("an option default outside its bounds is refused")
        {
            auto value = manifestJson("x");
            value["options"] = json::array({
                {{"key", "multiplier"}, {"type", "number"}, {"default", 0.0}, {"min", 0.5}},
            });
            const auto result = parseModManifest(value, "fixture");
            REQUIRE_FALSE(result);
            CHECK(result.getErr().code == ModErrorCode::InvalidOptionValue);
        }
    }

    TEST_CASE("resource directories are validated and are part of identity", "[setup][modsdk]")
    {
        auto value = manifestJson("com.example.prota");
        value["kind"] = "content";
        value["resourceDirectories"] = {
            {"weapons", "weaponP"},
            {"guis", "guiP"},
            {"unitpics", "unitpicsP"},
        };

        const auto parsed = parseModManifest(value, "fixture");
        REQUIRE(parsed);
        REQUIRE(parsed.get().resourceDirectories.size() == 3);
        CHECK(parsed.get().resourceDirectories.at("weapons") == "weaponP");

        SECTION("an unknown resource name is refused")
        {
            value["resourceDirectories"]["not-a-root"] = "x";
            const auto result = parseModManifest(value, "fixture");
            REQUIRE_FALSE(result);
            CHECK(result.getErr().code == ModErrorCode::InvalidField);
        }

        SECTION("a path that escapes the package is refused")
        {
            value["resourceDirectories"]["weapons"] = "../weaponP";
            const auto result = parseModManifest(value, "fixture");
            REQUIRE_FALSE(result);
            CHECK(result.getErr().code == ModErrorCode::InvalidField);
        }

        SECTION("the mapping changes the resolved identity")
        {
            const auto first = resolve({packageFrom(value)});
            REQUIRE(first);
            auto changed = value;
            changed["resourceDirectories"]["weapons"] = "weapons2";
            const auto second = resolve({packageFrom(changed)});
            REQUIRE(second);
            CHECK(second.get().identity != first.get().identity);
            CHECK(canonicalJson(resolvedModSetJson(first.get())).find("\"weaponP\"") != std::string::npos);
        }
    }

    TEST_CASE("a legacy descriptor borrows the caller's content-derived id", "[setup][modsdk]")
    {
        const json legacy = {{"name", "Old pack"}, {"version", "1.2.0"}};

        const auto without = parseModManifest(legacy, "legacy/fixture");
        REQUIRE_FALSE(without);
        CHECK(without.getErr().code == ModErrorCode::MissingField);

        const auto parsed = parseModManifest(legacy, "legacy/fixture", "legacy.fixture");
        REQUIRE(parsed);
        CHECK(parsed.get().legacy);
        CHECK(parsed.get().id == "legacy.fixture");
        CHECK(parsed.get().version == ModVersion{1, 2, 0});
        CHECK(parsed.get().kind == ModKind::Content);
    }

    TEST_CASE("dependency order is tie-broken by id, not by input order", "[setup][modsdk]")
    {
        auto base = manifestJson("a.base");
        base["version"] = "1.0.0";

        auto middle = manifestJson("b.middle");
        middle["requires"] = json::array({{{"id", "a.base"}, {"version", "^1.0.0"}}});

        auto top = manifestJson("c.top");
        top["requires"] = json::array({{{"id", "a.base"}}, {{"id", "b.middle"}}});
        top["features"] = json::array({"transform.definitions@1"});

        const auto environment = environmentWithFeatures({"transform.definitions@1"});

        const auto forward = resolve({packageFrom(base), packageFrom(middle), packageFrom(top)}, {}, environment);
        REQUIRE(forward);
        REQUIRE(forward.get().mods.size() == 3);
        CHECK(forward.get().mods[0].manifest.id == "a.base");
        CHECK(forward.get().mods[1].manifest.id == "b.middle");
        CHECK(forward.get().mods[2].manifest.id == "c.top");

        const auto backward = resolve({packageFrom(top), packageFrom(middle), packageFrom(base)}, {}, environment);
        REQUIRE(backward);
        CHECK(backward.get().identity == forward.get().identity);

        // A package's filesystem path is not simulation identity, so the same
        // bytes installed in two places resolve alike.
        const auto withSource = resolve(
            {packageFrom(base, "C:/Downloads/one"), packageFrom(middle), packageFrom(top)},
            {},
            environment);
        const auto withOtherSource = resolve(
            {packageFrom(base, "D:/Archive/two"), packageFrom(middle), packageFrom(top)},
            {},
            environment);
        REQUIRE(withSource);
        REQUIRE(withOtherSource);
        CHECK(withSource.get().identity == withOtherSource.get().identity);
    }

    TEST_CASE("resolution errors name the package and the failed relation", "[setup][modsdk]")
    {
        SECTION("missing dependency")
        {
            auto value = manifestJson("b.needs");
            value["requires"] = json::array({{{"id", "a.absent"}}});
            const auto result = resolve({packageFrom(value)});
            REQUIRE_FALSE(result);
            CHECK(result.getErr().code == ModErrorCode::MissingDependency);
            CHECK(result.getErr().packageId == "b.needs");
            CHECK(result.getErr().describe().find("a.absent") != std::string::npos);
        }

        SECTION("unsatisfied version")
        {
            auto dependency = manifestJson("a.base", "1.0.0");
            auto value = manifestJson("b.needs");
            value["requires"] = json::array({{{"id", "a.base"}, {"version", ">=2.0.0"}}});
            const auto result = resolve({packageFrom(dependency), packageFrom(value)});
            REQUIRE_FALSE(result);
            CHECK(result.getErr().code == ModErrorCode::UnsatisfiedDependency);
            CHECK(result.getErr().packageId == "b.needs");
            CHECK(result.getErr().describe().find("1.0.0") != std::string::npos);
        }

        SECTION("duplicate identity")
        {
            const auto result = resolve({packageFrom(manifestJson("a.same")), packageFrom(manifestJson("a.same"))});
            REQUIRE_FALSE(result);
            CHECK(result.getErr().code == ModErrorCode::DuplicateId);
        }

        SECTION("ordering cycle")
        {
            auto a = manifestJson("a.one");
            a["after"] = json::array({"b.two"});
            auto b = manifestJson("b.two");
            b["after"] = json::array({"a.one"});
            const auto result = resolve({packageFrom(a), packageFrom(b)});
            REQUIRE_FALSE(result);
            CHECK(result.getErr().code == ModErrorCode::DependencyCycle);
            CHECK(result.getErr().describe().find("a.one") != std::string::npos);
            CHECK(result.getErr().describe().find("b.two") != std::string::npos);
        }

        SECTION("unknown ordering reference")
        {
            auto a = manifestJson("a.one");
            a["before"] = json::array({"b.absent"});
            const auto result = resolve({packageFrom(a)});
            REQUIRE_FALSE(result);
            CHECK(result.getErr().code == ModErrorCode::UnknownOrderingReference);
            CHECK(result.getErr().describe().find("b.absent") != std::string::npos);
        }

        SECTION("unsupported feature and kind")
        {
            auto feature = manifestJson("a.feature");
            feature["features"] = json::array({"transform.definitions@9"});
            const auto featureResult = resolve({packageFrom(feature)});
            REQUIRE_FALSE(featureResult);
            CHECK(featureResult.getErr().code == ModErrorCode::UnsupportedFeature);

            // MOD-10 resolves presentation packages; a host that has not
            // enabled the kind still refuses one.
            auto presentation = manifestJson("a.presentation");
            presentation["kind"] = "presentation";
            const auto presentationResult = resolve({packageFrom(presentation)});
            REQUIRE(presentationResult);
            CHECK(presentationResult.get().mods.size() == 1);

            auto restrictedEnvironment = ModResolutionEnvironment{};
            restrictedEnvironment.supportedKinds = {ModKind::Content};
            const auto kindResult = resolve({packageFrom(presentation)}, {}, restrictedEnvironment);
            REQUIRE_FALSE(kindResult);
            CHECK(kindResult.getErr().code == ModErrorCode::UnsupportedKind);

            auto api = manifestJson("a.api");
            api["api"] = 2;
            const auto apiResult = resolve({packageFrom(api)});
            REQUIRE_FALSE(apiResult);
            CHECK(apiResult.getErr().code == ModErrorCode::UnsupportedApi);
        }
    }

    TEST_CASE("a keyed-exclusive key has one owner unless replaces says otherwise", "[setup][modsdk]")
    {
        SECTION("two claimants without a replacement are a load error")
        {
            auto first = manifestJson("a.first");
            first["exclusive"] = json::array({"hook.damage.v1"});
            auto second = manifestJson("b.second");
            second["exclusive"] = json::array({"hook.damage.v1"});

            const auto result = resolve({packageFrom(first), packageFrom(second)});
            REQUIRE_FALSE(result);
            CHECK(result.getErr().code == ModErrorCode::ExclusiveConflict);
            CHECK(result.getErr().packageId == "b.second");
            CHECK(result.getErr().describe().find("a.first:hook.damage.v1") != std::string::npos);
        }

        SECTION("a declared replacement moves ownership")
        {
            auto first = manifestJson("a.first");
            first["exclusive"] = json::array({"hook.damage.v1"});
            auto second = manifestJson("b.second");
            second["exclusive"] = json::array({"hook.damage.v1"});
            second["replaces"] = json::array({"a.first:hook.damage.v1"});

            const auto result = resolve({packageFrom(first), packageFrom(second)});
            if (!result)
            {
                UNSCOPED_INFO(result.getErr().describe());
            }
            REQUIRE(result);
            REQUIRE(result.get().mods.size() == 2);
            CHECK(result.get().mods[0].ownedExclusiveKeys.empty());
            REQUIRE(result.get().mods[1].ownedExclusiveKeys.size() == 1);
            CHECK(result.get().mods[1].ownedExclusiveKeys[0] == "hook.damage.v1");

            const auto canonical = resolvedModSetJson(result.get());
            CHECK(canonical["packages"][0]["exclusive"].empty());
            CHECK(canonical["packages"][1]["exclusive"][0] == "hook.damage.v1");
        }
    }

    TEST_CASE("options resolve from defaults and the player's selection", "[setup][modsdk]")
    {
        auto value = manifestJson("a.options");
        value["options"] = json::array({
            {{"key", "multiplier"}, {"type", "number"}, {"default", 1.0}, {"min", 0.25}, {"max", 4.0}},
            {{"key", "enabled"}, {"type", "boolean"}, {"default", true}},
            {{"key", "mode"}, {"type", "string"}, {"choices", json::array({"fast", "careful"})}, {"default", "careful"}},
            {{"key", "required"}, {"type", "string"}},
        });

        const auto defaults = resolve({packageFrom(value)});
        REQUIRE_FALSE(defaults);
        CHECK(defaults.getErr().code == ModErrorCode::MissingOptionValue);
        CHECK(defaults.getErr().describe().find("required") != std::string::npos);

        ModOptionSelections selections;
        selections["a.options"]["required"] = "yes";
        const auto resolved = resolve({packageFrom(value)}, selections);
        REQUIRE(resolved);
        const auto& options = resolved.get().mods[0].options;
        CHECK(options.at("multiplier").get<double>() == 1.0);
        CHECK(options.at("enabled").get<bool>());
        CHECK(options.at("mode").get<std::string>() == "careful");
        CHECK(options.at("required").get<std::string>() == "yes");

        ModOptionSelections changed;
        changed["a.options"]["required"] = "yes";
        changed["a.options"]["multiplier"] = 2.5;
        const auto changedSet = resolve({packageFrom(value)}, changed);
        REQUIRE(changedSet);
        CHECK(changedSet.get().mods[0].options.at("multiplier").get<double>() == 2.5);
        CHECK(changedSet.get().identity != resolved.get().identity);

        ModOptionSelections outOfRange;
        outOfRange["a.options"]["required"] = "yes";
        outOfRange["a.options"]["multiplier"] = 9.0;
        const auto ranged = resolve({packageFrom(value)}, outOfRange);
        REQUIRE_FALSE(ranged);
        CHECK(ranged.getErr().code == ModErrorCode::InvalidOptionValue);
        CHECK(ranged.getErr().describe().find("multiplier") != std::string::npos);

        ModOptionSelections unknown;
        unknown["a.options"]["required"] = "yes";
        unknown["a.options"]["not-declared"] = true;
        const auto unknownResult = resolve({packageFrom(value)}, unknown);
        REQUIRE_FALSE(unknownResult);
        CHECK(unknownResult.getErr().code == ModErrorCode::UnknownOption);

        ModOptionSelections strayPackage;
        strayPackage["a.other"]["required"] = "yes";
        const auto strayResult = resolve({packageFrom(value)}, strayPackage);
        REQUIRE_FALSE(strayResult);
        CHECK(strayResult.getErr().code == ModErrorCode::UnknownPackage);
    }

    TEST_CASE("the canonical set is compact, sorted and covers manifest bytes", "[setup][modsdk]")
    {
        const auto value = manifestJson("a.package");
        const auto raw = value.dump(2);
        const auto package = modPackageFromJson(value, raw, "some/source");
        REQUIRE(package);

        const auto first = resolve({package.get()});
        REQUIRE(first);
        const auto text = canonicalJson(resolvedModSetJson(first.get()));
        CHECK(text.find('\n') == std::string::npos);
        CHECK(text.find(' ') == std::string::npos);
        CHECK(text.compare(0, 1, "{") == 0);
        CHECK(text.find("\"format\":\"rwe.resolved-mods\"") != std::string::npos);
        CHECK(first.get().identity.size() == 64);
        CHECK(sha256Hex(text) == first.get().identity);

        // Whitespace in the author file is part of the manifest bytes.
        const auto spaced = modPackageFromJson(value, raw + "\n", "some/source");
        REQUIRE(spaced);
        const auto second = resolve({spaced.get()});
        REQUIRE(second);
        CHECK(second.get().identity != first.get().identity);

        // An empty set is resolvable and carries its own identity.
        const auto empty = resolve({});
        REQUIRE(empty);
        CHECK(empty.get().mods.empty());
        CHECK(empty.get().identity.size() == 64);
        CHECK(canonicalJson(resolvedModSetJson(empty.get()))
            == "{\"api\":1,\"format\":\"rwe.resolved-mods\",\"formatVersion\":1,\"packages\":[]}");
    }
}
