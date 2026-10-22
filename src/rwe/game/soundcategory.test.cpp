#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameMediaDatabase.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>
#include <cstdlib>

namespace rwe
{
    TEST_CASE("unit sound categories use case-insensitive names and ordered numeric bindings", "[soundcategory]")
    {
        // BT-133; names and select/acknowledgement slots from the first two
        // categories of retail rev31.gp3 gamedata/SOUND.TDF.
        const auto sounds = parseSoundTdf(
            "[ARM_KBOT] { select1=kbarmsel; ok1=kbarmmov; }"
            "[COR_KBOT] { select1=kbcorsel; ok1=kbcormov; }");
        REQUIRE(sounds.size() == 2);
        REQUIRE(sounds[0].first == "ARM_KBOT");
        REQUIRE(sounds[1].first == "COR_KBOT");
        GameMediaDatabase media;
        for (auto sound : sounds)
        {
            media.addSoundClass(sound.first, std::move(sound.second));
        }

        SECTION("retail corvoyr spelling matches COR_KBOT")
        {
            auto fbi = parseUnitFbi(parseTdfFromString(
                "[UNITINFO] { UnitName=CORVOYR; Objectname=CORVOYR; SoundCategory=COR_Kbot; }"));
            REQUIRE(media.getSoundClassOrDefault(fbi.soundCategory).select1 == "kbcorsel");
            REQUIRE(media.getSoundClass("cor_kbot").ok1 == "kbcormov");
        }

        SECTION("retail unmatched names and absent bindings select category zero")
        {
            for (const auto* name : {"CORE_MEX", "CORE_KBOT", "COR_TANK", "NONE", ""})
            {
                REQUIRE(media.getSoundClassOrDefault(name).select1 == "kbarmsel");
                REQUIRE(media.getSoundClassOrDefault(name).ok1 == "kbarmmov");
            }
        }

        SECTION("numeric bindings use file order and numeric prefixes")
        {
            REQUIRE(media.getSoundClassOrDefault("0").select1 == "kbarmsel");
            REQUIRE(media.getSoundClassOrDefault("1").select1 == "kbcorsel");
            REQUIRE(media.getSoundClassOrDefault("  +1suffix").select1 == "kbcorsel");
        }

        SECTION("invalid indices are safely silent")
        {
            for (const auto* name : {"-1", "2", "9999999999999999999999999"})
            {
                REQUIRE_FALSE(media.getSoundClassOrDefault(name).select1);
            }
        }

        SECTION("a duplicate name does not consume an index")
        {
            media.addSoundClass("arm_kbot", SoundClass{});
            REQUIRE(media.getSoundClassOrDefault("0").select1 == "kbarmsel");
            REQUIRE_FALSE(media.getSoundClassOrDefault("2").select1);
        }
    }

    TEST_CASE("an empty sound category table is safely silent", "[soundcategory]")
    {
        GameMediaDatabase media;
        REQUIRE_FALSE(media.getSoundClassOrDefault("").select1);
        REQUIRE_FALSE(media.getSoundClassOrDefault("NONE").select1);
    }

    TEST_CASE("shipped sound categories bind through the game VFS", "[soundcategory][retail]")
    {
        const auto* dataPath = std::getenv("RWE_SCENARIO_DATA");
        if (!dataPath)
        {
            SKIP("set RWE_SCENARIO_DATA to the retail archive directory");
        }
        auto vfs = constructVfs(dataPath);
        auto bytes = vfs.readFile("gamedata/SOUND.TDF");
        REQUIRE(bytes);
        auto sounds = parseSoundTdf(std::string(bytes->begin(), bytes->end()));
        REQUIRE(sounds.size() >= 2);
        REQUIRE(sounds[0].first == "ARM_KBOT");
        REQUIRE(sounds[1].first == "COR_KBOT");
        GameMediaDatabase media;
        for (auto sound : sounds)
        {
            media.addSoundClass(sound.first, std::move(sound.second));
        }
        for (std::size_t i = 0; i < sounds.size(); ++i)
        {
            CAPTURE(i, sounds[i].first);
            REQUIRE(media.getSoundClassOrDefault(std::to_string(i)).select1 == sounds[i].second.select1);
            REQUIRE(media.getSoundClassOrDefault(sounds[i].first).ok1 == sounds[i].second.ok1);
        }
        for (const auto* unit : {"cormex", "corvoyr"})
        {
            CAPTURE(unit);
            auto fbiBytes = vfs.readFile(std::string("units/") + unit + ".fbi");
            REQUIRE(fbiBytes);
            auto fbi = parseUnitFbi(parseTdfFromBytes(*fbiBytes));
            const auto expected = fbi.unitName == "CORMEX" ? "ARM_KBOT" : "COR_KBOT";
            REQUIRE(media.getSoundClassOrDefault(fbi.soundCategory).select1 == media.getSoundClass(expected).select1);
        }
    }
}
