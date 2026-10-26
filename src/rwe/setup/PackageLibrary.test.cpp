#include <catch2/catch_test_macros.hpp>
#include <rwe/setup/PackageLibrary.h>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace rwe
{
    namespace
    {
        struct PackageFixture
        {
            fs::path root = fs::temp_directory_path()
                / ("rwe-package-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                    + "-" + std::to_string(std::random_device{}()));

            PackageFixture()
            {
                if (!fs::create_directory(root))
                {
                    throw std::runtime_error("Cannot create unique package test directory");
                }
            }

            ~PackageFixture()
            {
                std::error_code error;
                const auto parent = fs::weakly_canonical(root.parent_path(), error);
                if (!error && parent == fs::weakly_canonical(fs::temp_directory_path(), error)
                    && root.filename().string().compare(0, 17, "rwe-package-test-") == 0)
                {
                    fs::remove_all(root, error);
                }
            }

            fs::path write(const fs::path& name, const std::string& contents)
            {
                const auto path = root / name;
                fs::create_directories(path.parent_path());
                std::ofstream file(path, std::ios::binary);
                file << contents;
                file.close();
                return path;
            }

            std::string read(const fs::path& name)
            {
                std::ifstream file(root / name, std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(file), {});
            }
        };

        json registryFixture()
        {
            const auto id = "p-" + std::string(32, 'a');
            return {{"schemaVersion", 1}, {"packages", json::array({{
                {"id", id}, {"name", "Example mod"}, {"kind", "mod"}, {"version", "1.0"},
                {"state", "installed"}, {"compatibility", "unverified"}, {"managed", true},
                {"source", "original-download.ufo"}, {"contentPath", "packages/" + id},
                {"dependencies", json::array()}, {"resourceDirectories", {{"units", "units"}, {"builderGuis", "guiE"}}},
                {"files", json::array({{{"path", "content.ufo"}, {"size", 3}, {"sha256", std::string(64, 'a')}}})}
            }})}};
        }
    }

    TEST_CASE("package registry reads are non-destructive and updates replace an existing file", "[setup][packages]")
    {
        PackageFixture fixture;
        const auto filename = fixture.root / "Library" / "registry.json";
        CHECK(loadPackageRegistry(filename) == emptyPackageRegistry());
        CHECK_FALSE(fs::exists(filename.parent_path()));

        auto registry = registryFixture();
        registry["annotation"] = "preserve unknown metadata";
        savePackageRegistry(filename, registry);
        CHECK(loadPackageRegistry(filename) == registry);
        registry["packages"][0]["name"] = "Renamed mod";
        savePackageRegistry(filename, registry);
        CHECK(loadPackageRegistry(filename) == registry);
        CHECK(std::distance(fs::directory_iterator(filename.parent_path()), fs::directory_iterator{}) == 1);

        const auto saved = fixture.read("Library/registry.json");
        auto invalid = registry;
        invalid["schemaVersion"] = 2;
        CHECK_THROWS(savePackageRegistry(filename, invalid));
        CHECK(fixture.read("Library/registry.json") == saved);
        fixture.write("Library/registry.json", "{broken");
        CHECK_THROWS(loadPackageRegistry(filename));
        CHECK(fixture.read("Library/registry.json") == "{broken");
    }

    TEST_CASE("package registry validates identity and file ownership independently of labels", "[setup][packages]")
    {
        auto registry = registryFixture();
        SECTION("two versions can share a display name while their IDs differ")
        {
            auto other = registry["packages"][0];
            other["id"] = "p-" + std::string(32, 'b');
            other["contentPath"] = "packages/p-" + std::string(32, 'b');
            registry["packages"].push_back(other);
            CHECK_NOTHROW(validatePackageRegistry(registry));
        }
        SECTION("duplicate IDs cannot hide behind different labels")
        {
            registry["packages"].push_back(registry["packages"][0]);
            registry["packages"][1]["name"] = "Different label";
            CHECK_THROWS(validatePackageRegistry(registry));
        }
        SECTION("external installations remain explicitly unmanaged")
        {
            registry["packages"][0]["managed"] = false;
            registry["packages"][0].erase("contentPath");
            registry["packages"][0]["externalPath"] = fs::temp_directory_path().string();
            CHECK_NOTHROW(validatePackageRegistry(registry));
        }
        SECTION("managed destinations must belong to their own IDs")
        {
            registry["packages"][0]["contentPath"] = "../Original TA";
            CHECK_THROWS(validatePackageRegistry(registry));
        }
        SECTION("file manifests reject traversal and Windows aliases")
        {
            for (const auto* path : {"../source.ufo", "/source.ufo", "C:/source.ufo", "a\\b", "a//b", "a/./b", "a/../b"})
            {
                registry["packages"][0]["files"][0]["path"] = path;
                CHECK_THROWS(validatePackageRegistry(registry));
            }
        }
        SECTION("case aliases cannot identify two owned files")
        {
            auto file = registry["packages"][0]["files"][0];
            file["path"] = "CONTENT.UFO";
            registry["packages"][0]["files"].push_back(file);
            CHECK_THROWS(validatePackageRegistry(registry));
        }
        SECTION("compatibility cannot be asserted by an arbitrary string")
        {
            registry["packages"][0]["compatibility"] = "all mods supported";
            CHECK_THROWS(validatePackageRegistry(registry));
        }
        SECTION("self-dependencies are invalid")
        {
            registry["packages"][0]["dependencies"].push_back(registry["packages"][0]["id"]);
            CHECK_THROWS(validatePackageRegistry(registry));
        }
    }

    TEST_CASE("package inspection discovers nested resource roots without changing source files", "[setup][packages]")
    {
        PackageFixture fixture;
        const auto data = fs::path("Delivery") / "Actual data with spaces";
        fixture.write(data / "units" / "ARMFOO.fbi", "[UNITINFO]{UnitName=ARMFOO;Objectname=ARMFOO;}");
        fixture.write(data / "Music" / "song.ogg", "companion asset");
        const auto metadata = fixture.write(data / "rwe_mod.json", R"({"shortname":"test","name":"The real mod name","version":"2.1"})");
        fixture.write("Delivery/README.txt", "How to install the package.");
        const auto original = fixture.read(data / "rwe_mod.json");
        const auto modified = fs::last_write_time(metadata);
        const auto report = inspectContentPackage(fixture.root / "Delivery");
        REQUIRE(report["candidates"].size() == 1);
        const auto candidate = report["candidates"][0];
        CHECK(candidate["kind"] == "mod");
        CHECK(candidate["name"] == "The real mod name");
        CHECK(candidate["version"] == "2.1");
        CHECK(candidate["recognition"] == "rwe_mod.json");
        CHECK(candidate["compatibility"] == "unverified");
        CHECK(candidate["fileCount"] == 3);
        CHECK(report["complete"] == true);
        CHECK(fixture.read(data / "rwe_mod.json") == original);
        CHECK(fs::last_write_time(metadata) == modified);
        CHECK_FALSE(fs::exists(fixture.root / "registry.json"));
    }

    TEST_CASE("package inspection routes Escalation builder pages to their separate GUI root", "[setup][packages]")
    {
        PackageFixture fixture;
        fixture.write("Escalation/AE/marker.txt", "");
        fixture.write("Escalation/UNITSE/ARMFOO.fbi", "[UNITINFO]{UnitName=ARMFOO;Objectname=ARMFOO;}");
        fixture.write("Escalation/WEAPONE/ARMWEAPON.fbi", "[WEAPON]{}");
        fixture.write("Escalation/GAMEDATE/marker.txt", "");
        fixture.write("Escalation/GUIE/ARMACK2.GUI", "builder page");
        const auto report = inspectContentPackage(fixture.root / "Escalation");
        REQUIRE(report["candidates"].size() == 1);
        CHECK(report["candidates"][0]["resourceDirectories"]["builderGuis"] == "guie");
        CHECK_FALSE(report["candidates"][0]["resourceDirectories"].contains("guis"));
    }
    TEST_CASE("loose map selection finds its case-insensitive partner without importing sibling maps", "[setup][packages]")
    {
        PackageFixture fixture;
        const auto ota = fixture.write("Blue Planet.OTA", "[GlobalHeader]{missionname=Blue Planet;SCHEMACOUNT=0;}");
        fixture.write("blue planet.tnt", "terrain validation is a separate task");
        fixture.write("Other.ota", "other map");
        auto report = inspectContentPackage(ota);
        REQUIRE(report["candidates"].size() == 1);
        const auto candidate = report["candidates"][0];
        CHECK(candidate["kind"] == "maps");
        REQUIRE(candidate["maps"].size() == 1);
        CHECK(candidate["maps"][0]["paired"] == true);
        CHECK(candidate["maps"][0]["validation"] == "not-run");
        CHECK(candidate["compatibility"] == "unverified");
        report = inspectContentPackage(fixture.root / "Other.ota");
        CHECK(report["candidates"][0]["maps"][0]["paired"] == false);
    }

    TEST_CASE("package inspection reports deferred extraction and damaged input explicitly", "[setup][packages]")
    {
        PackageFixture fixture;
        SECTION("ZIP RAR and 7z wait for the bundled extraction task")
        {
            for (const auto* name : {"maps.zip", "mod.RAR", "pack.7z"})
            {
                const auto file = fixture.write(name, "not extracted by the inspection foundation");
                const auto report = inspectContentPackage(file);
                CHECK(report["complete"] == false);
                CHECK(report["candidates"].empty());
                CHECK(report["containers"][0]["state"] == "extraction-required");
            }
        }
        SECTION("bad TA archives do not become successful inspections")
        {
            const auto file = fixture.write("broken.ufo", "broken");
            const auto report = inspectContentPackage(file);
            CHECK(report["complete"] == false);
            CHECK_FALSE(report["candidates"][0]["warnings"].empty());
            CHECK(fixture.read("broken.ufo") == "broken");
        }
        SECTION("bad display metadata is reported")
        {
            fixture.write("rwe_mod.json", "{broken");
            const auto report = inspectContentPackage(fixture.root);
            CHECK(report["complete"] == false);
        }
        SECTION("missing input throws instead of installing anything")
        {
            CHECK_THROWS(inspectContentPackage(fixture.root / "missing"));
        }
    }

    TEST_CASE("loose original AI profiles retain their own package category", "[setup][packages]")
    {
        PackageFixture fixture;
        fixture.write("Default.txt", "plan hard\nweight ARM .1\nlimit ARMSOLAR 12\n");
        const auto report = inspectContentPackage(fixture.root);
        REQUIRE(report["candidates"].size() == 1);
        CHECK(report["candidates"][0]["kind"] == "ai");
        CHECK(report["candidates"][0]["compatibility"] == "unverified");
    }
}
