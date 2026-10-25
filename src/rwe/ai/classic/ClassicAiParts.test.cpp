#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/classic/ClassicAiGroups.h>
#include <rwe/ai/classic/ClassicAiScoring.h>
#include <rwe/ai/classic/ClassicAiScript.h>
#include <rwe/ai/classic/ClassicRandom.h>
#include <rwe/io/tdf/tdf.h>

namespace rwe
{
    TEST_CASE("downloadable AI metadata follows the original FBI and first-row rules", "[ai][classic][mods]")
    {
        // MOD-AI-02/03 and DOWNLOAD-LINK-01, including the shipped BLANK pattern.
        for (const auto flag : {-2, -1, 0, 1, 2, 3})
        {
            const auto info = parseTdfFromString("[UNITINFO]{UnitName=ARMAAA;Downloadable=" + std::to_string(flag) + ";ai_weight=weight ARMAAA 0;}");
            const auto script = parseClassicDownloadableScript(info.findBlock("UNITINFO")->get());
            CHECK(script.downloadable == ((flag & 1) != 0));
            CHECK(script.text == "weight ARMAAA 0");
        }
        auto info = parseTdfFromString("[UNITINFO]{UnitName=ARMAAA;ai_weight=" + std::string(80, 'x') + ";}");
        CHECK(parseClassicDownloadableScript(info.findBlock("UNITINFO")->get()).text.size() == 63);
        CHECK_FALSE(parseClassicDownloadableScript(info.findBlock("UNITINFO")->get()).downloadable);
        std::vector<ClassicDownloadableScript> scripts{{"ARMAAA", "weight ARMAAA 0", false}};
        const std::set<std::string> types{"ARMACV", "ARMAAA"};
        promoteDownloadableAiScript(scripts, parseListTdfFromString(
            "[FIRST]{UNITMENU=BLANK;UNITNAME=ARMAAA;}[SECOND]{UNITMENU=ARMACV;UNITNAME=ARMAAA;}"), types);
        CHECK_FALSE(scripts[0].downloadable);
        promoteDownloadableAiScript(scripts, parseListTdfFromString(
            "[FIRST]{UNITMENU=ARMACV;UNITNAME=ARMAAA;}"), types);
        CHECK(scripts[0].downloadable);
    }

    TEST_CASE("downloadable AI fragments execute whole commands in independent lock passes", "[ai][classic][mods]")
    {
        // Deliberately not UNITNAME order: the original sorts IDs before these passes.
        const std::vector<ClassicScriptType> types{{"B", "GROUP"}, {"A", "GROUP"}};
        std::vector<ClassicTypeEntry> entries(2);
        SECTION("category adjustments can execute again in the limit pass")
        {
            applyClassicDownloadableScripts({{"A", "weight GROUP .5", true}}, ClassicPlan::Hard, types, entries);
            CHECK(entries[0].multiplier == 25);
            CHECK(entries[1].multiplier == 25);
        }
        SECTION("UNITNAME order and target locks govern cross-unit commands")
        {
            applyClassicDownloadableScripts({{"B", "weight B .8", true}, {"A", "weight B .5", true}}, ClassicPlan::Hard, types, entries);
            CHECK(entries[0].multiplier == 50);
            CHECK(entries[1].multiplier == 100);
        }
        SECTION("a weight-locked owner can still execute both commands in the limit pass")
        {
            entries[1].multiplier = 80;
            entries[1].multiplierLocked = true;
            applyClassicDownloadableScripts({{"A", "weight A .5\nlimit A 3", true}}, ClassicPlan::Hard, types, entries);
            CHECK(entries[1].multiplier == 80);
            CHECK(entries[1].limit == 3);
            CHECK(entries[1].limitLocked);
        }
        SECTION("each pass starts enabled while a fragment's plan persists within its pass")
        {
            entries[1].limitLocked = true;
            applyClassicDownloadableScripts({{"A", "plan easy", true}, {"B", "weight B 0", true}}, ClassicPlan::Hard, types, entries);
            CHECK(entries[0].multiplier == 0);
        }
        SECTION("a non-downloadable fragment is ignored")
        {
            applyClassicDownloadableScripts({{"A", "weight B 0", false}}, ClassicPlan::Hard, types, entries);
            CHECK(entries[0].multiplier == 100);
        }
    }

    namespace
    {
        // The first values of the Park-Miller minimal standard from state 1.
        const std::uint32_t ParkMillerSequence[] = {16807u, 282475249u, 1622650073u, 984943658u, 1144108930u};

        UnitId uid(unsigned int v)
        {
            return UnitId(v);
        }
    }

    // ---------------------------------------------------------------- the draw

    TEST_CASE("the classic draw is Park-Miller and exact", "[ai][classic]")
    {
        auto rng = ClassicRandom::fromState(1);
        for (auto expected : ParkMillerSequence)
        {
            // Modulo the modulus itself, rand(2^31 - 1) is the raw state.
            CHECK(rng.below(2147483647u) == expected);
        }

        // The textbook check: the 10000th value from seed 1 is 1043618065.
        auto check = ClassicRandom::fromState(1);
        std::uint32_t value = 0;
        for (int i = 0; i < 10000; ++i)
        {
            value = check.below(2147483647u);
        }
        CHECK(value == 1043618065u);
    }

    TEST_CASE("rand(n) below two returns zero without advancing", "[ai][classic]")
    {
        auto rng = ClassicRandom::fromState(12345);
        const auto before = rng.current();
        CHECK(rng.below(0) == 0u);
        CHECK(rng.below(1) == 0u);
        CHECK(rng.current() == before);

        // Two is the first n that draws.
        rng.below(2);
        CHECK(rng.current() != before);
        CHECK(rng.current() == static_cast<std::uint32_t>((static_cast<std::uint64_t>(before) * 16807u) % 2147483647u));
    }

    TEST_CASE("the classic draw is seeded from (input ^ 0x66e29572) | 1", "[ai][classic]")
    {
        // 0 ^ 0x66e29572 = 1726125426, | 1 = 1726125427, reduced into [1, 2^31 - 2].
        CHECK(ClassicRandom::fromSeed(0).current() == 1726125428u);

        // The same seed gives the same sequence, and a different one does not.
        auto a = ClassicRandom::fromSeed(987654321ull);
        auto b = ClassicRandom::fromSeed(987654321ull);
        auto c = ClassicRandom::fromSeed(987654322ull);
        bool differs = false;
        for (int i = 0; i < 20; ++i)
        {
            auto x = a.below(1000);
            CHECK(x == b.below(1000));
            differs = differs || x != c.below(1000);
        }
        CHECK(differs);

        // Every seed lands in [1, 2^31 - 2].
        for (std::uint64_t seed : {0ull, 1ull, 0x66e29572ull, 0xffffffffull, 0xffffffffffffffffull, 2147483646ull})
        {
            auto s = ClassicRandom::fromSeed(seed).current();
            CHECK(s >= 1u);
            CHECK(s <= 2147483646u);
        }
    }

    // ---------------------------------------------------------------- the script

    TEST_CASE("the profile reader takes three directives and ignores the rest", "[ai][classic]")
    {
        auto d = parseClassicScript("// a comment\nPLAN Hard   # trailing\n  weight ARMSOLAR 2.5\r\nlimit armmex 4\nbogus thing 3\n\n");
        REQUIRE(d.size() == 3);
        CHECK(d[0].kind == ClassicScriptDirective::Kind::Plan);
        CHECK(d[0].args == std::vector<std::string>{"HARD"});
        CHECK(d[1].kind == ClassicScriptDirective::Kind::Weight);
        CHECK(d[1].args == std::vector<std::string>{"ARMSOLAR", "2.5"});
        CHECK(d[2].kind == ClassicScriptDirective::Kind::Limit);
        CHECK(d[2].args == std::vector<std::string>{"ARMMEX", "4"});
    }

    TEST_CASE("the profile reader's atof", "[ai][classic]")
    {
        CHECK(classicParseNumber("0.5") == 0.5);
        CHECK(classicParseNumber("18.35") == 18.35);
        CHECK(classicParseNumber("-3") == -3.0);
        CHECK(classicParseNumber("12abc") == 12.0);
        CHECK(classicParseNumber("abc") == 0.0);
        CHECK(classicParseNumber("1e2") == 100.0);
        CHECK(classicParseNumber(".25") == 0.25);
        CHECK(classicParseNumber("0.29") == 0.29);
    }

    namespace
    {
        std::vector<ClassicScriptType> scriptTypes()
        {
            return {
                {"ARMSOLAR", "ARM LEVEL1 ENERGY"},
                {"ARMMEX", "ARM LEVEL1 METAL"},
                {"CORSOLAR", "CORE LEVEL1 ENERGY"},
                {"ARMLAB", "ARM LEVEL1 PLANT"},
            };
        }

        std::vector<ClassicTypeEntry> run(const std::string& text, ClassicPlan plan, ClassicScriptReport* report = nullptr)
        {
            auto types = scriptTypes();
            std::vector<ClassicTypeEntry> entries(types.size());
            auto r = applyClassicScript(parseClassicScript(text), plan, types, entries);
            if (report)
            {
                *report = r;
            }
            return entries;
        }
    }

    TEST_CASE("every type starts at multiplier 100, no limit, unlocked", "[ai][classic]")
    {
        auto entries = run("", ClassicPlan::Hard);
        for (const auto& e : entries)
        {
            CHECK(e.multiplier == 100);
            CHECK(e.limit == -1);
            CHECK(!e.multiplierLocked);
            CHECK(!e.limitLocked);
        }
    }

    TEST_CASE("plan gates the weight and limit directives that follow it", "[ai][classic]")
    {
        const std::string text =
            "weight armsolar 0.5\n" // before the first plan: every difficulty
            "plan easy\n"
            "weight armmex 0.2\n"
            "plan hard\n"
            "limit armlab 3\n"
            "plan any\n"
            "weight corsolar 0.1\n";

        auto easy = run(text, ClassicPlan::Easy);
        CHECK(easy[0].multiplier == 50);
        CHECK(easy[1].multiplier == 20);
        CHECK(easy[3].limit == -1);
        CHECK(easy[2].multiplier == 10);

        auto medium = run(text, ClassicPlan::Medium);
        CHECK(medium[0].multiplier == 50);
        CHECK(medium[1].multiplier == 100);
        CHECK(medium[3].limit == -1);
        CHECK(medium[2].multiplier == 10);

        auto hard = run(text, ClassicPlan::Hard);
        CHECK(hard[0].multiplier == 50);
        CHECK(hard[1].multiplier == 100);
        CHECK(hard[3].limit == 3);
        CHECK(hard[2].multiplier == 10);
    }

    TEST_CASE("a name locks and a category does not", "[ai][classic]")
    {
        SECTION("a repeated name is dead, which is all first-occurrence-wins is")
        {
            auto e = run("weight armsolar 0.5\nweight armsolar 0.1\nlimit armsolar 7\nlimit armsolar 2\n", ClassicPlan::Hard);
            CHECK(e[0].multiplier == 50);
            CHECK(e[0].multiplierLocked);
            CHECK(e[0].limit == 7);
            CHECK(e[0].limitLocked);
        }

        SECTION("a category is applied again to anything unlocked")
        {
            auto e = run("weight level1 0.5\nweight level1 0.5\n", ClassicPlan::Hard);
            for (const auto& entry : e)
            {
                CHECK(entry.multiplier == 25);
                CHECK(!entry.multiplierLocked);
            }
        }

        SECTION("a category rule lands on units a name has not claimed")
        {
            auto e = run("weight armsolar 0.5\nweight energy 0.5\n", ClassicPlan::Hard);
            CHECK(e[0].multiplier == 50); // claimed by name: the category skips it
            CHECK(e[2].multiplier == 50); // CORSOLAR is ENERGY too and is unclaimed
            CHECK(e[1].multiplier == 100);
        }

        SECTION("a name after a category still applies, and then locks")
        {
            auto e = run("weight level1 0.5\nweight armmex 0.2\nweight armmex 0.9\n", ClassicPlan::Hard);
            CHECK(e[1].multiplier == 10);
            CHECK(e[1].multiplierLocked);
        }

        SECTION("the multiplier lock and the limit lock are separate")
        {
            auto e = run("weight armmex 0.5\nlimit armmex 4\nlimit armmex 9\nweight armmex 0.1\n", ClassicPlan::Hard);
            CHECK(e[1].multiplier == 50);
            CHECK(e[1].limit == 4);
        }

        SECTION("ALL is a category every type is in, listed or not (BT-78)")
        {
            auto e = run("weight all 0.5\nlimit all 4\n", ClassicPlan::Hard);
            for (const auto& entry : e)
            {
                CHECK(entry.multiplier == 50);
                CHECK(entry.limit == 4);
                CHECK(!entry.multiplierLocked);
            }
        }

        SECTION("ALL does not override a name that claimed the type")
        {
            auto e = run("weight armmex 0.2\nweight all 0.5\n", ClassicPlan::Hard);
            CHECK(e[1].multiplier == 20);
            CHECK(e[0].multiplier == 50);
        }

        SECTION("the side names are categories too")
        {
            auto e = run("weight core 0.1\n", ClassicPlan::Hard);
            CHECK(e[2].multiplier == 10);
            CHECK(e[0].multiplier == 100);
        }
    }

    TEST_CASE("a weight is truncated and clamped to 0..100", "[ai][classic]")
    {
        auto e = run("weight armsolar 18.35\nweight armmex 0.0099\nweight corsolar 0.29\nweight armlab -2\n", ClassicPlan::Hard);
        CHECK(e[0].multiplier == 100);
        CHECK(e[1].multiplier == 0);
        // 100 * 0.29 is 28.999999999999996 in a double, and a truncation is a truncation.
        CHECK(e[2].multiplier == 28);
        CHECK(e[3].multiplier == 0);
    }

    TEST_CASE("a missing number on a limit or weight line is zero, and a negative limit blocks (BT-128)", "[ai][classic]")
    {
        SECTION("Limit with a subject and no number forbids the type")
        {
            auto e = run("limit armmex\n", ClassicPlan::Hard);
            CHECK(e[1].limit == 0);
            CHECK(e[1].limitLocked);
            CHECK(e[0].limit == -1);
        }

        SECTION("Weight with a subject and no number zeroes it")
        {
            auto e = run("weight armsolar\n", ClassicPlan::Hard);
            CHECK(e[0].multiplier == 0);
            CHECK(e[1].multiplier == 100);
        }

        SECTION("a category with no number takes the zero too")
        {
            auto e = run("limit core\n", ClassicPlan::Hard);
            CHECK(e[2].limit == 0);
            CHECK(e[0].limit == -1);
        }

        SECTION("a line with no subject changes nothing")
        {
            auto e = run("limit\nweight\n", ClassicPlan::Hard);
            for (const auto& entry : e)
            {
                CHECK(entry.limit == -1);
                CHECK(entry.multiplier == 100);
            }
        }

        SECTION("a negative limit other than -1 is kept, and -1 is still unlimited")
        {
            auto e = run("limit armsolar -1\nlimit armmex -2\nlimit corsolar -7.9\n", ClassicPlan::Hard);
            CHECK(e[0].limit == -1);
            CHECK(e[1].limit == -2);
            CHECK(e[2].limit == -7);
        }

        SECTION("the rating of a type with a negative limit below -1 is zero")
        {
            ClassicEconomy economy;
            economy.energy = 900.0;
            economy.metal = 450.0;
            economy.energyStorage = 1000.0;
            economy.metalStorage = 500.0;
            economy.netEnergy = 10.0;
            economy.netMetal = 10.0;
            economy.energyIncome = 300.0;
            economy.metalIncome = 10.0;
            ClassicRatings ratings;
            ratings.normal = 100;
            CHECK(classicFinalRating(ratings, economy, 100, -1, 0) > 0);
            CHECK(classicFinalRating(ratings, economy, 100, -2, 0) == 0);
        }

        SECTION("any counts only as the first word after plan")
        {
            auto e = run("plan easy any\nweight armsolar 0.5\n", ClassicPlan::Hard);
            CHECK(e[0].multiplier == 100);
            auto first = run("plan any easy\nweight armsolar 0.5\n", ClassicPlan::Hard);
            CHECK(first[0].multiplier == 50);
            auto easy = run("plan easy any\nweight armsolar 0.5\n", ClassicPlan::Easy);
            CHECK(easy[0].multiplier == 50);
        }
    }

    TEST_CASE("a name that matches nothing is reported once and changes nothing", "[ai][classic]")
    {
        ClassicScriptReport report;
        auto e = run("weight nosuchthing 0.5\nlimit nosuchthing 3\nweight armmex 0.5\n", ClassicPlan::Hard, &report);
        REQUIRE(report.unknownNames.size() == 1);
        CHECK(report.unknownNames[0] == "NOSUCHTHING");
        CHECK(e[1].multiplier == 50);
    }

    // ---------------------------------------------------------------- the scores

    namespace
    {
        ClassicTypeFacts metalExtractor()
        {
            ClassicTypeFacts f;
            f.extractor = true;
            f.energyUse = 3.0;
            f.metalCost = 50.0;
            f.energyCost = 500.0;
            f.waterDepthWord = false;
            return f;
        }

        ClassicTypeFacts solarPanel()
        {
            ClassicTypeFacts f;
            f.energyMake = 20.0;
            f.metalCost = 155.0;
            f.waterDepthWord = false;
            return f;
        }

        ClassicRatingContext context(int completedOfType, int completedTotal = 0, bool lowWind = false)
        {
            ClassicRatingContext c;
            c.completedOfType = completedOfType;
            c.completedTotal = completedTotal;
            c.unitLimit = 200;
            c.lowWind = lowWind;
            return c;
        }
    }

    TEST_CASE("the weapon score", "[ai][classic]")
    {
        CHECK(classicWeaponScore(false, {}) == 1);
        CHECK(classicWeaponScore(true, {}) == 11);
        // 11 + 100/40 (2) + 300/100 (3) + 5.
        CHECK(classicWeaponScore(true, {{100u, 300.0}}) == 21);
        // The divisions are integer: 79/40 is 1 and 399/100 is 3.
        CHECK(classicWeaponScore(true, {{79u, 399.0}}) == 11 + 1 + 3 + 5);
        // Two slots add twice.
        CHECK(classicWeaponScore(true, {{100u, 300.0}, {40u, 100.0}}) == 11 + (2 + 3 + 5) + (1 + 1 + 5));
        // Clamped to [-100, 100].
        CHECK(classicWeaponScore(true, {{40000u, 9000.0}}) == 100);
    }

    TEST_CASE("the type value", "[ai][classic]")
    {
        SECTION("a metal extractor")
        {
            // 11 + 0 + 0 + trunc(0.5) + trunc(1.0) + 1.
            CHECK(classicTypeValue(metalExtractor(), 1) == 13);
        }

        SECTION("a solar panel")
        {
            // 1 + 10 (negative net energy use) + trunc(1.55) + 0 + 1.
            CHECK(classicTypeValue(solarPanel(), 1) == 13);
        }

        SECTION("a commander-class builder")
        {
            ClassicTypeFacts f;
            f.canAttack = true;
        f.waterDepthWord = false;
            f.builder = true;
            f.mobile = true;
            f.energyMake = 25.0;
            f.metalCost = 2500.0;
            f.energyCost = 25000.0;
            auto weapon = classicWeaponScore(true, {{100u, 300.0}});
            CHECK(weapon == 21);
            // 1 + 10 + 25 + 50 + 21.
            CHECK(classicTypeValue(f, weapon) == 107);
        }

        SECTION("a fighter")
        {
            ClassicTypeFacts f;
            f.canAttack = true;
        f.waterDepthWord = false;
            f.canFly = true;
            f.mobile = true;
            f.metalCost = 60.0;
            f.energyCost = 800.0;
            auto weapon = classicWeaponScore(true, {{80u, 400.0}});
            CHECK(weapon == 22);
            // 1 + trunc(0.6) + trunc(1.6) + 22.
            CHECK(classicTypeValue(f, weapon) == 24);
        }

        SECTION("the value is a signed byte")
        {
            ClassicTypeFacts f;
            f.metalCost = 1.0e6;
            CHECK(classicTypeValue(f, 100) == 127);
        }
    }

    TEST_CASE("the ratings of a metal extractor", "[ai][classic]")
    {
        auto f = metalExtractor();

        // Normal: 1 + 50 (extractor) = 51; energy it produces 0.
        auto three = classicRatings(f, 13, context(3));
        CHECK(three.normal == 51);
        // Metal: -0.02 * 50 + 100 = 99. Energy: -0.0025 * 500 - 5 * 3 < 0, so 0.
        CHECK(three.metal == 99);
        CHECK(three.energy == 0);

        // None standing: x4 is 204, capped at 100.
        CHECK(classicRatings(f, 13, context(0)).normal == 100);
        // One standing: x2 is 102, capped too.
        CHECK(classicRatings(f, 13, context(1)).normal == 100);
        // Two standing is plain.
        CHECK(classicRatings(f, 13, context(2)).normal == 51);
    }

    TEST_CASE("the ratings of a solar panel", "[ai][classic]")
    {
        auto f = solarPanel();
        auto r = classicRatings(f, 13, context(2));
        // 1 + 50 (net energy use -20) + 20 (what it makes, in [0, 30]).
        CHECK(r.normal == 71);
        // Metal: -0.02 * 155 is below zero. Energy: -5 * -20 = 100.
        CHECK(r.metal == 0);
        CHECK(r.energy == 100);

        // What it makes is clamped to 30.
        f.energyMake = 500.0;
        CHECK(classicRatings(f, 13, context(2)).normal == 1 + 50 + 30);
    }

    TEST_CASE("the ratings of a commander-class builder", "[ai][classic]")
    {
        ClassicTypeFacts f;
        f.canAttack = true;
        f.waterDepthWord = false;
        f.builder = true;
        f.mobile = true;
        f.energyMake = 25.0;
        f.metalCost = 2500.0;
        f.energyCost = 25000.0;

        // Fewer than three exist: +30. 21 + 30 + 50 + 25 = 126, capped.
        CHECK(classicRatings(f, 107, context(2)).normal == 100);
        // Three exist: 21 + 50 + 25 = 96.
        CHECK(classicRatings(f, 107, context(3)).normal == 96);
        // Metal and energy are both clamped at zero below.
        auto r = classicRatings(f, 107, context(3));
        CHECK(r.metal == 0);
        // -0.0025 * 25000 - 5 * -25 = -62.5 + 125 = 62.5.
        CHECK(r.energy == 62);
    }

    TEST_CASE("the ratings of a fighter", "[ai][classic]")
    {
        ClassicTypeFacts f;
        f.canAttack = true;
        f.waterDepthWord = false;
        f.canFly = true;
        f.mobile = true;
        f.metalCost = 60.0;
        f.energyCost = 800.0;

        // 21 + 40.
        CHECK(classicRatings(f, 24, context(3, 10)).normal == 61);

        // Over half the unit limit (150 > 100): half the type value, 24 / 2.
        CHECK(classicRatings(f, 24, context(3, 150)).normal == 61 + 12);
        // Exactly half is not over.
        CHECK(classicRatings(f, 24, context(3, 100)).normal == 61);
    }

    TEST_CASE("a wind generator rates zero in low wind and not otherwise", "[ai][classic]")
    {
        ClassicTypeFacts f;
        f.windGenerator = true;
        f.waterDepthWord = false;
        f.metalCost = 40.0;
        CHECK(classicRatings(f, 1, context(3, 0, false)).normal == 1);
        CHECK(classicRatings(f, 1, context(3, 0, true)).normal == 0);
    }

    TEST_CASE("low wind is the map's maximum below 2500", "[ai][classic]")
    {
        // BT-120: not a mean. A map of 1500 to 2400 has a mean of 1950, which
        // the old test (mean below 1250) called windy and the original calls
        // low; 0 is the absent key.
        CHECK(classicLowWind(0));
        CHECK(classicLowWind(2400));
        CHECK(classicLowWind(2499));
        CHECK_FALSE(classicLowWind(2500));
        CHECK_FALSE(classicLowWind(3000));
    }

    TEST_CASE("a transport and a feature rate zero", "[ai][classic]")
    {
        ClassicTypeFacts f;
        f.mobile = true;
        f.canFly = true;
        f.waterDepthWord = false;
        CHECK(classicRatings(f, 1, context(3)).normal == 1 + 40);
        f.canLoad = true;
        CHECK(classicRatings(f, 1, context(3)).normal == 0);
        f.canLoad = false;
        f.isFeature = true;
        CHECK(classicRatings(f, 1, context(3)).normal == 0);
    }

    TEST_CASE("the water-depth word multiplies the normal rating by three", "[ai][classic]")
    {
        ClassicTypeFacts f;
        f.waterDepthWord = false;
        CHECK(classicRatings(f, 1, context(3)).normal == 1);
        f.waterDepthWord = true;
        CHECK(classicRatings(f, 1, context(3)).normal == 3);

        // The three multipliers combine one after the other: 1 * 4 * 3.
        CHECK(classicRatings(f, 1, context(0)).normal == 12);
        // 1 * 2 * 3.
        CHECK(classicRatings(f, 1, context(1)).normal == 6);
    }

    TEST_CASE("the shortage weights", "[ai][classic]")
    {
        SECTION("a metal shortage claims priority")
        {
            ClassicEconomy e;
            e.energy = 500.0;
            e.metal = 100.0;
            e.energyStorage = 1000.0;
            e.metalStorage = 500.0;
            e.netEnergy = 5.0;
            e.netMetal = 5.0;
            e.energyIncome = 100.0;
            e.metalIncome = 4.0;
            // E = 62 + 10 = 72. M = 100 + 20 = 120, clamped to 100, and it
            // leaves nothing of E.
            auto s = classicShortage(e);
            CHECK(s.metalWeight == 100);
            CHECK(s.energyWeight == 0);
            CHECK(s.normalWeight == 0);
        }

        SECTION("a comfortable economy leans on the normal rating")
        {
            ClassicEconomy e;
            e.energy = 900.0;
            e.metal = 450.0;
            e.energyStorage = 1000.0;
            e.metalStorage = 500.0;
            e.netEnergy = 10.0;
            e.netMetal = 10.0;
            e.energyIncome = 300.0;
            e.metalIncome = 10.0;
            // E = trunc(12.5) = 12, M = trunc(12.5) = 12.
            auto s = classicShortage(e);
            CHECK(s.metalWeight == 12);
            CHECK(s.energyWeight == 0);
            CHECK(s.normalWeight == 88);
        }

        SECTION("an empty economy clamps every weight")
        {
            ClassicEconomy e;
            e.energyStorage = 1000.0;
            e.metalStorage = 500.0;
            // E = 125 + 20 + 100 = 245, M = 125 + 20 + 100 = 245.
            auto s = classicShortage(e);
            CHECK(s.metalWeight == 100);
            CHECK(s.energyWeight == 100);
            CHECK(s.normalWeight == 0);
        }

        SECTION("storage is capped at 1000 energy and 500 metal in the formula")
        {
            ClassicEconomy e;
            e.energy = 50.0;
            e.metal = 10.0;
            e.energyStorage = 5000.0;
            e.metalStorage = 100.0;
            e.netEnergy = 5.0;
            e.netMetal = 5.0;
            e.energyIncome = 500.0;
            e.metalIncome = 20.0;
            // E = trunc((1000 - 50) * 0.125) = 118, M = trunc((100 - 10) * 0.25) = 22.
            auto s = classicShortage(e);
            CHECK(s.metalWeight == 22);
            CHECK(s.energyWeight == 96);
            CHECK(s.normalWeight == 0);
        }

        SECTION("the income steps")
        {
            ClassicEconomy e;
            e.energy = 1000.0;
            e.metal = 500.0;
            e.energyStorage = 1000.0;
            e.metalStorage = 500.0;
            e.netEnergy = 5.0;
            e.netMetal = 5.0;
            e.energyIncome = 49.0;
            e.metalIncome = 2.0;
            // Nothing missing, so E and M are only the income bumps.
            auto s = classicShortage(e);
            CHECK(s.metalWeight == 100);
            CHECK(s.energyWeight == 0);

            e.energyIncome = 100.0;
            e.metalIncome = 4.0;
            s = classicShortage(e);
            CHECK(s.metalWeight == 20);
            CHECK(s.energyWeight == 0); // E = 10, less the 20 already given to metal

            e.energyIncome = 500.0;
            e.metalIncome = 50.0;
            s = classicShortage(e);
            CHECK(s.metalWeight == 0);
            CHECK(s.energyWeight == 0);
            CHECK(s.normalWeight == 100);
        }
    }

    TEST_CASE("the final rating", "[ai][classic]")
    {
        ClassicEconomy e;
        e.energy = 900.0;
        e.metal = 450.0;
        e.energyStorage = 1000.0;
        e.metalStorage = 500.0;
        e.netEnergy = 10.0;
        e.netMetal = 10.0;
        e.energyIncome = 300.0;
        e.metalIncome = 10.0;

        ClassicRatings r;
        r.normal = 100;
        r.metal = 0;
        r.energy = 0;

        // normalWeight 88: 88 * 100 * 100 / 10000.
        CHECK(classicFinalRating(r, e, 100, -1, 0) == 88);
        // The script multiplier scales it.
        CHECK(classicFinalRating(r, e, 50, -1, 0) == 44);

        // Integer division can take a small preference to zero: 88 * 1 * 1 / 10000.
        r.normal = 1;
        CHECK(classicFinalRating(r, e, 1, -1, 0) == 0);
        r.normal = 7;
        // 88 * 7 = 616, * 100 / 10000 = 6.16 -> 6.
        CHECK(classicFinalRating(r, e, 100, -1, 0) == 6);
    }

    TEST_CASE("the final rating's gates", "[ai][classic]")
    {
        ClassicEconomy e;
        e.energy = 900.0;
        e.metal = 450.0;
        e.energyStorage = 1000.0;
        e.metalStorage = 500.0;
        e.netEnergy = 10.0;
        e.netMetal = 10.0;
        e.energyIncome = 300.0;
        e.metalIncome = 10.0;
        ClassicRatings r;
        r.normal = 100;

        CHECK(classicFinalRating(r, e, 100, -1, 0) == 88);

        // A limit is a bound on COMPLETED units: allowed while completed < limit.
        CHECK(classicFinalRating(r, e, 100, 3, 2) == 88);
        CHECK(classicFinalRating(r, e, 100, 3, 3) == 0);
        CHECK(classicFinalRating(r, e, 100, 0, 0) == 0);

        // Under 50 stored energy, or under 25 stored metal, nothing is built.
        e.energy = 49.0;
        CHECK(classicFinalRating(r, e, 100, -1, 0) == 0);
        e.energy = 50.0;
        e.metal = 24.0;
        CHECK(classicFinalRating(r, e, 100, -1, 0) == 0);
        e.metal = 25.0;
        r.metal = 100;
        CHECK(classicFinalRating(r, e, 100, -1, 0) > 0);
    }

    TEST_CASE("the weighted draw consumes one draw per positive candidate", "[ai][classic]")
    {
        auto rng = ClassicRandom::fromState(1);

        // Positive candidates are 0, 2 and 4. Draws, from state 1:
        //   total 10: 16807 % 10 = 7 < 10 -> index 0
        //   total 15: 282475249 % 15 = 4 < 5 -> index 2
        //   total 35: 1622650073 % 35 = 23, not < 20 -> stays 2
        auto chosen = classicWeightedDraw({10, 0, 5, -3, 20}, rng);
        CHECK(chosen == 2);
        CHECK(rng.current() == 1622650073u); // exactly three values were drawn
    }

    TEST_CASE("the weighted draw with nothing to draw", "[ai][classic]")
    {
        auto rng = ClassicRandom::fromState(777);
        const auto before = rng.current();
        CHECK(classicWeightedDraw({}, rng) == -1);
        CHECK(classicWeightedDraw({0, -1, 0}, rng) == -1);
        CHECK(rng.current() == before);

        // One candidate at rating 1: rand(1) is 0 without advancing, and 0 < 1.
        CHECK(classicWeightedDraw({0, 1}, rng) == 1);
        CHECK(rng.current() == before);
    }

    TEST_CASE("the weighted draw is weighted", "[ai][classic]")
    {
        auto rng = ClassicRandom::fromSeed(2024);
        int first = 0;
        const int rounds = 4000;
        for (int i = 0; i < rounds; ++i)
        {
            if (classicWeightedDraw({90, 10}, rng) == 0)
            {
                ++first;
            }
        }
        // About 90%, which is far from the 50% a flat draw would give.
        CHECK(first > 3400);
        CHECK(first < 3800);
    }

    // ---------------------------------------------------------------- the groups

    TEST_CASE("units sort into groups in the register's order", "[ai][classic]")
    {
        ClassicClassifyFacts f;

        // A building: armed is 5, unarmed is 1.
        f = {};
        f.building = true;
        f.armed = true;
        CHECK(classicClassify(f) == ClassicGroup::Defences);
        f.armed = false;
        CHECK(classicClassify(f) == ClassicGroup::Buildings);

        // A factory is a building and a builder: the building test is first.
        f = {};
        f.building = true;
        f.builder = true;
        CHECK(classicClassify(f) == ClassicGroup::Buildings);

        // A mobile builder is 4, and armed does not change it.
        f = {};
        f.builder = true;
        f.armed = true;
        CHECK(classicClassify(f) == ClassicGroup::Builders);

        // An aircraft builder is 4, not 8.
        f = {};
        f.builder = true;
        f.canFly = true;
        CHECK(classicClassify(f) == ClassicGroup::Builders);

        // Every other aircraft is 8, armed or not.
        f = {};
        f.canFly = true;
        CHECK(classicClassify(f) == ClassicGroup::Aircraft);
        f.armed = true;
        CHECK(classicClassify(f) == ClassicGroup::Aircraft);

        // Water, armed or not, before the armed test.
        f = {};
        f.waterDepthPositive = true;
        CHECK(classicClassify(f) == ClassicGroup::WaterReserve);
        f.armed = true;
        CHECK(classicClassify(f) == ClassicGroup::WaterReserve);

        // Armed and mobile is 3; an armed amphibious unit is 7 first.
        f = {};
        f.armed = true;
        CHECK(classicClassify(f) == ClassicGroup::LandReserve);

        // An unarmed land unit -- a carrier -- is in no group at all.
        f = {};
        CHECK(classicClassify(f) == ClassicGroup::None);
    }

    TEST_CASE("membership removes by swapping the last member into the hole", "[ai][classic]")
    {
        ClassicGroups g;
        g.assign(uid(1), ClassicGroup::LandReserve);
        g.assign(uid(2), ClassicGroup::LandReserve);
        g.assign(uid(3), ClassicGroup::LandReserve);
        g.assign(uid(4), ClassicGroup::LandReserve);
        REQUIRE(g.members(ClassicGroup::LandReserve) == std::vector<UnitId>{uid(1), uid(2), uid(3), uid(4)});

        CHECK(g.remove(uid(1)));
        CHECK(g.members(ClassicGroup::LandReserve) == std::vector<UnitId>{uid(4), uid(2), uid(3)});
        CHECK(g.groupOf(uid(1)) == ClassicGroup::None);
        CHECK(!g.remove(uid(1)));

        // Moving a unit to another group takes it out of the first.
        g.assign(uid(2), ClassicGroup::Builders);
        CHECK(g.members(ClassicGroup::LandReserve) == std::vector<UnitId>{uid(4), uid(3)});
        CHECK(g.groupOf(uid(2)) == ClassicGroup::Builders);
    }

    namespace
    {
        struct Layout
        {
            std::map<unsigned int, ClassicPosition> at;
            std::function<ClassicPosition(UnitId)> fn()
            {
                return [this](UnitId id) { return at.at(id.value); };
            }
        };
    }

    TEST_CASE("an empty assault takes the reserve's first unit and admits the rest that fit", "[ai][classic]")
    {
        ClassicGroups g;
        Layout l;
        l.at[1] = {0.0, 0.0};
        l.at[2] = {10.0, 0.0};
        l.at[3] = {20.0, 0.0};
        g.assign(uid(1), ClassicGroup::LandReserve);
        g.assign(uid(2), ClassicGroup::LandReserve);
        g.assign(uid(3), ClassicGroup::LandReserve);

        classicRebalanceAssault(g, ClassicGroup::LandAssault, ClassicGroup::LandReserve, 20000.0, l.fn());
        CHECK(g.members(ClassicGroup::LandAssault).size() == 3);
        CHECK(g.members(ClassicGroup::LandReserve).empty());
        // The first reserve unit led the way.
        CHECK(g.members(ClassicGroup::LandAssault).front() == uid(1));
    }

    TEST_CASE("a unit beyond the cohesion criterion goes back to the reserve, keeping one", "[ai][classic]")
    {
        ClassicGroups g;
        Layout l;
        l.at[1] = {0.0, 0.0};
        l.at[2] = {0.0, 0.0};
        l.at[3] = {1000.0, 0.0};
        g.assign(uid(1), ClassicGroup::LandAssault);
        g.assign(uid(2), ClassicGroup::LandAssault);
        g.assign(uid(3), ClassicGroup::LandAssault);

        classicRebalanceAssault(g, ClassicGroup::LandAssault, ClassicGroup::LandReserve, 20000.0, l.fn());
        CHECK(g.groupOf(uid(3)) == ClassicGroup::LandReserve);
        CHECK(g.groupOf(uid(1)) == ClassicGroup::LandAssault);
        CHECK(g.groupOf(uid(2)) == ClassicGroup::LandAssault);
    }

    TEST_CASE("the rebalance keeps the source's stale-position quirk", "[ai][classic]")
    {
        // F is a long way out and first in the vector; A and B are at home.
        // Removing F swaps B into slot 0, and the source then subtracts the
        // position of whatever is in slot 0 -- B's, not F's -- so the running
        // sum still carries F. The centroid stays out at 2500, A and B are
        // both "far" from it, B is shed as well, and F, back in the reserve,
        // is now at the centroid's own position and is taken in again.
        // A port that subtracts the leaver's own position ends with F in the
        // reserve and A and B in the assault instead.
        ClassicGroups g;
        Layout l;
        const unsigned int F = 1, A = 2, B = 3;
        l.at[F] = {5000.0, 0.0};
        l.at[A] = {0.0, 0.0};
        l.at[B] = {0.0, 0.0};
        g.assign(uid(F), ClassicGroup::LandAssault);
        g.assign(uid(A), ClassicGroup::LandAssault);
        g.assign(uid(B), ClassicGroup::LandAssault);

        classicRebalanceAssault(g, ClassicGroup::LandAssault, ClassicGroup::LandReserve, 20000.0, l.fn());

        CHECK(g.groupOf(uid(F)) == ClassicGroup::LandAssault);
        CHECK(g.groupOf(uid(A)) == ClassicGroup::LandAssault);
        CHECK(g.groupOf(uid(B)) == ClassicGroup::LandReserve);
    }

    TEST_CASE("a centroid is the mean of a group", "[ai][classic]")
    {
        ClassicGroups g;
        Layout l;
        l.at[1] = {0.0, 0.0};
        l.at[2] = {100.0, 40.0};
        ClassicPosition out;
        CHECK(!classicCentroid(g, ClassicGroup::Buildings, l.fn(), out));
        g.assign(uid(1), ClassicGroup::Buildings);
        g.assign(uid(2), ClassicGroup::Buildings);
        REQUIRE(classicCentroid(g, ClassicGroup::Buildings, l.fn(), out));
        CHECK(out.x == 50.0);
        CHECK(out.z == 20.0);
    }
}
