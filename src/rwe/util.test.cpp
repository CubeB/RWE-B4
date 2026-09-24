#include <catch2/catch_test_macros.hpp>
#include <rwe/util.h>
#include <set>

namespace rwe
{
    TEST_CASE("the user data directory follows the XDG base directory rule", "[util]")
    {
        namespace fs = std::filesystem;

        SECTION("without XDG_DATA_HOME the data home is ~/.local/share, and ~/.rwe is the fallback")
        {
            auto c = localDataPathCandidates(nullptr, "/home/someone");
            REQUIRE(c == std::vector<fs::path>{"/home/someone/.local/share/rwe", "/home/someone/.rwe"});
        }

        SECTION("an absolute XDG_DATA_HOME is honoured")
        {
            auto c = localDataPathCandidates("/mnt/data", "/home/someone");
            REQUIRE(c.front() == fs::path("/mnt/data/rwe"));
            REQUIRE(c.back() == fs::path("/home/someone/.rwe"));
        }

        SECTION("an empty or relative XDG_DATA_HOME is invalid, and the default applies")
        {
            REQUIRE(localDataPathCandidates("", "/home/someone").front() == fs::path("/home/someone/.local/share/rwe"));
            REQUIRE(localDataPathCandidates("share", "/home/someone").front() == fs::path("/home/someone/.local/share/rwe"));
        }

        SECTION("no HOME means no data directory at all")
        {
            REQUIRE(localDataPathCandidates(nullptr, nullptr).empty());
            REQUIRE(localDataPathCandidates("/mnt/data", "").empty());
            REQUIRE_FALSE(chooseLocalDataPath({}, [](const fs::path&) { return true; }).has_value());
        }

        SECTION("an existing ~/.rwe keeps being used until the new location exists")
        {
            auto c = localDataPathCandidates(nullptr, "/home/someone");
            auto existing = [](std::set<fs::path> dirs) {
                return [dirs = std::move(dirs)](const fs::path& p) { return dirs.count(p) != 0; };
            };

            // A fresh machine: the new place.
            REQUIRE(*chooseLocalDataPath(c, existing({})) == fs::path("/home/someone/.local/share/rwe"));
            // An old install: where its files are.
            REQUIRE(*chooseLocalDataPath(c, existing({"/home/someone/.rwe"})) == fs::path("/home/someone/.rwe"));
            // Both, after someone has moved: the new place wins.
            REQUIRE(*chooseLocalDataPath(c, existing({"/home/someone/.rwe", "/home/someone/.local/share/rwe"})) == fs::path("/home/someone/.local/share/rwe"));
        }
    }
}
