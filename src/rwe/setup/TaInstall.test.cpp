#include <catch2/catch_test_macros.hpp>
#include <rwe/setup/TaInstall.h>
#include <algorithm>
#include <cctype>

namespace rwe
{
    namespace
    {
        /**
         * A GOG Commander Pack install, as it actually sits on disk: thirty
         * archives at the root, the five films in `Data` with the mixed casing
         * the release ships, and the soundtrack in `music`.
         */
        TaInstallListing makeGogListing()
        {
            TaInstallListing listing;
            listing.root = "C:/GOG Games/Total Annihilation";
            listing.files = {
                {"", "totala1.hpi", 100},
                {"", "totala2.hpi", 200},
                {"", "totala3.hpi", 50},
                {"", "rev31.gp3", 300},
                {"", "ccdata.ccx", 400},
                {"", "ccmaps.ccx", 500},
                {"", "worlds.hpi", 60},
                {"", "AFark.ufo", 7},
                {"", "TotalA.exe", 1178624},
                {"", "EULA.txt", 12},
                {"", "gog.ico", 34},
                {"Data", "1.ZRB", 1000},
                {"Data", "2.zrb", 2000},
                {"music", "0.mp3", 10},
                {"music", "1.mp3", 11},
            };
            return listing;
        }
    }

    TEST_CASE("the archive extensions are the ones the engine actually mounts", "[setup]")
    {
        // The list here exists so the setup can find archives without a VFS,
        // and it is only useful while it agrees with the VFS. If someone adds
        // an extension to addToVfs and not here, the setup would silently
        // leave those files behind and the game would load without them.
        for (const auto& extension : taArchiveExtensions())
        {
            REQUIRE(extension.front() == '.');

            // Lower-cased, because classifyTaFile lower-cases what it
            // compares against them.
            auto hasUpper = std::any_of(extension.begin(), extension.end(), [](unsigned char c) {
                return std::isupper(c) != 0;
            });
            REQUIRE_FALSE(hasUpper);
        }

        // The five in addToVfs, as of writing.
        REQUIRE(taArchiveExtensions() == std::vector<std::string>{".hpi", ".ufo", ".ccx", ".gpf", ".gp3"});
    }

    TEST_CASE("files are classified by where they are and what they are called", "[setup]")
    {
        SECTION("archives are taken from the root only")
        {
            REQUIRE(classifyTaFile("", "totala1.hpi") == TaFileKind::Archive);
            REQUIRE(classifyTaFile("", "rev31.gp3") == TaFileKind::Archive);
            REQUIRE(classifyTaFile("", "ccdata.ccx") == TaFileKind::Archive);

            // A mod's own archive inside its own directory is that mod's
            // business; flattening several of those together would collide.
            REQUIRE(classifyTaFile("mods", "something.ufo") == TaFileKind::Ignored);
        }

        SECTION("case does not matter")
        {
            REQUIRE(classifyTaFile("", "TOTALA1.HPI") == TaFileKind::Archive);
            REQUIRE(classifyTaFile("Data", "1.ZRB") == TaFileKind::Movie);
            REQUIRE(classifyTaFile("MUSIC", "0.MP3") == TaFileKind::Music);
        }

        SECTION("films are found by extension, not by directory")
        {
            // They live in `Data` in the GOG release, but that directory can
            // hold other things, so the extension is what decides.
            REQUIRE(classifyTaFile("Data", "2.zrb") == TaFileKind::Movie);
            REQUIRE(classifyTaFile("", "intro.smk") == TaFileKind::Movie);
            REQUIRE(classifyTaFile("Data", "readme.txt") == TaFileKind::Ignored);
        }

        SECTION("music is only music where music lives")
        {
            REQUIRE(classifyTaFile("music", "0.mp3") == TaFileKind::Music);
            REQUIRE(classifyTaFile("", "0.mp3") == TaFileKind::Ignored);
        }

        SECTION("the rest of an installation is left alone")
        {
            REQUIRE(classifyTaFile("", "TotalA.exe") == TaFileKind::Ignored);
            REQUIRE(classifyTaFile("", "EULA.txt") == TaFileKind::Ignored);
            REQUIRE(classifyTaFile("", "noextension") == TaFileKind::Ignored);
        }
    }

    TEST_CASE("each kind of file goes where the engine looks for it", "[setup]")
    {
        // Archives sit flat, because that is the one directory the VFS scans
        // for them; the films and the soundtrack go into the subdirectories
        // the engine asks the VFS for by name.
        REQUIRE(taFileDestination(TaFileKind::Archive, "totala1.hpi") == std::filesystem::path("totala1.hpi"));
        REQUIRE(taFileDestination(TaFileKind::Movie, "2.zrb") == std::filesystem::path("movies") / "2.zrb");
        REQUIRE(taFileDestination(TaFileKind::Music, "0.mp3") == std::filesystem::path("music") / "0.mp3");

        // Casing is carried across untouched. The GOG release ships `1.ZRB`
        // beside `2.zrb`, and the VFS resolves either -- see
        // findPathCaseInsensitive -- so renaming would be work for nothing.
        REQUIRE(taFileDestination(TaFileKind::Movie, "1.ZRB") == std::filesystem::path("movies") / "1.ZRB");
    }

    TEST_CASE("an installation is recognised by the archives the engine needs", "[setup]")
    {
        REQUIRE(looksLikeTaInstall(makeGogListing()));

        SECTION("a directory missing the base game is not one")
        {
            auto listing = makeGogListing();
            listing.files.erase(listing.files.begin());
            REQUIRE_FALSE(looksLikeTaInstall(listing));
        }

        SECTION("nor is an empty directory")
        {
            TaInstallListing empty;
            REQUIRE_FALSE(looksLikeTaInstall(empty));
        }

        SECTION("case does not matter here either")
        {
            TaInstallListing listing;
            listing.files = {{"", "TOTALA1.HPI", 1}, {"", "TotalA2.hpi", 1}};
            REQUIRE(looksLikeTaInstall(listing));
        }
    }

    TEST_CASE("the plan copies every archive, film and track, and nothing else", "[setup]")
    {
        auto listing = makeGogListing();
        auto plan = planTaSetup(listing, "C:/data", {});

        auto countOf = [&](TaFileKind kind) {
            return std::count_if(plan.items.begin(), plan.items.end(), [&](const auto& i) { return i.kind == kind; });
        };

        REQUIRE(countOf(TaFileKind::Archive) == 8);
        REQUIRE(countOf(TaFileKind::Movie) == 2);
        REQUIRE(countOf(TaFileKind::Music) == 2);
        REQUIRE(plan.items.size() == 12);
        REQUIRE(plan.missingRequired.empty());
        REQUIRE(plan.missingRecommended.empty());

        // 100+200+50+300+400+500+60+7 archives, 3000 films, 21 music.
        REQUIRE(plan.bytesToCopy == 1617 + 3000 + 21);

        SECTION("sources and destinations are both right")
        {
            auto film = std::find_if(plan.items.begin(), plan.items.end(), [](const auto& i) {
                return i.source.filename() == "2.zrb";
            });
            REQUIRE(film != plan.items.end());
            REQUIRE(film->source == std::filesystem::path("C:/GOG Games/Total Annihilation") / "Data" / "2.zrb");
            REQUIRE(film->destination == std::filesystem::path("C:/data") / "movies" / "2.zrb");
        }
    }

    TEST_CASE("a second run copies nothing", "[setup]")
    {
        auto listing = makeGogListing();
        auto first = planTaSetup(listing, "C:/data", {});

        // What the first run would have left behind.
        std::vector<std::tuple<std::string, std::string, std::uintmax_t>> existing;
        for (const auto& item : first.items)
        {
            auto relative = item.destination.parent_path().filename().string();
            auto directory = (relative == "data") ? std::string() : relative;
            existing.emplace_back(directory, item.destination.filename().string(), item.size);
        }

        auto second = planTaSetup(listing, "C:/data", existing);

        REQUIRE(second.alreadyPresentCount == first.items.size());
        REQUIRE(second.bytesToCopy == 0);

        SECTION("but a half-written file is copied again")
        {
            // Matched on name and size: TA data does not change once
            // installed, so a size mismatch means an interrupted copy.
            existing.front() = {std::get<0>(existing.front()), std::get<1>(existing.front()), 1};
            auto third = planTaSetup(listing, "C:/data", existing);
            REQUIRE(third.alreadyPresentCount == first.items.size() - 1);
            REQUIRE(third.bytesToCopy > 0);
        }
    }

    TEST_CASE("the plan says what an installation is missing", "[setup]")
    {
        TaInstallListing bare;
        bare.files = {{"", "totala1.hpi", 1}, {"", "totala2.hpi", 1}};

        auto plan = planTaSetup(bare, "C:/data", {});

        REQUIRE(plan.missingRequired.empty());

        // rev31 is the data the behavioural work is written against, so its
        // absence is worth saying out loud even though the game still runs.
        REQUIRE(plan.missingRecommended
            == std::vector<std::string>{"rev31.gp3", "ccdata.ccx", "ccmaps.ccx"});

        SECTION("and refuses one that cannot work at all")
        {
            TaInstallListing empty;
            auto emptyPlan = planTaSetup(empty, "C:/data", {});
            REQUIRE(emptyPlan.missingRequired
                == std::vector<std::string>{"totala1.hpi", "totala2.hpi"});
        }
    }
}
