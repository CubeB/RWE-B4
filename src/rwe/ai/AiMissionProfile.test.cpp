#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/ai_test_util.h>

namespace rwe
{
    TEST_CASE("mission caps reserve simultaneous factory orders and weights change the combat mix", "[ai][mission-profile]")
    {
        // #383 translation: exercise the real controller with two idle labs,
        // rather than testing a copy of the gate or of the share arithmetic.
        GameSimulation sim(makeFlatTerrain(64, 64), 7u, 0, 0);
        auto ai = addPlayer(sim, "AI", GamePlayerType::Computer, "ARM");
        addPlayer(sim, "enemy", GamePlayerType::Human, "CORE");
        defineWorld(sim);
        auto script = makeEmptyCobScript();
        addUnit(sim, "ARMCOM", ai, SimVector(-200_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 0_ss), script);
        auto profile = makeDefaultStandardProfile();
        profile.buildPlannerTickInterval = 1;
        profile.counterEnemyComposition = false;
        profile.labRaiderShare = 2;
        profile.labRocketKbotShare = 1;
        profile.labArtilleryKbotShare = 0;
        auto tree = makeBuildTree();

        auto plannedTypes = [&](const AiBuildTree& rules) {
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, rules);
            sim.gameTime = GameTime(30);
            std::vector<PlayerCommand> commands;
            controller.tick(sim, commands);
            std::vector<std::string> result;
            for (const auto& command : commands)
            {
                if (auto unit = std::get_if<PlayerUnitCommand>(&command))
                {
                    if (auto queue = std::get_if<PlayerUnitCommand::ModifyBuildQueue>(&unit->command))
                    {
                        if (queue->count > 0)
                        {
                            result.push_back(queue->unitType);
                        }
                    }
                }
            }
            return result;
        };

        SECTION("two laboratories share one reserved constructor slot")
        {
            profile.targetConstructorCount = 3;
            tree.missionRules["ARMCK"] = ClassicTypeEntry{100, 1};
            auto types = plannedTypes(tree);
            CHECK(std::count(types.begin(), types.end(), "ARMCK") == 1);
            tree.missionRules["ARMCK"].limit = 0;
            types = plannedTypes(tree);
            CHECK(std::count(types.begin(), types.end(), "ARMCK") == 0);
        }

        SECTION("a lower rocket multiplier shifts production toward raiders")
        {
            profile.targetConstructorCount = 0;
            tree.missionRules["ARMCK"] = ClassicTypeEntry{100, 0};
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMPW", ai, SimVector(-100_ss, 0_ss, intToSimScalar(100 + i * 40)), script);
            }
            addUnit(sim, "ARMROCK", ai, SimVector(100_ss, 0_ss, 100_ss), script);
            auto baseline = plannedTypes(tree);
            REQUIRE(std::count(baseline.begin(), baseline.end(), "ARMROCK") > 0);
            tree.missionRules["ARMROCK"] = ClassicTypeEntry{25, -1};
            auto weighted = plannedTypes(tree);
            CHECK(std::count(weighted.begin(), weighted.end(), "ARMPW") > 0);
            CHECK(std::count(weighted.begin(), weighted.end(), "ARMROCK") == 0);
        }
    }
}
