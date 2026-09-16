#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitMesh.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <rwe/sim/tad_build_episodes.h>
#include <set>
#include <string>
#include <vector>

namespace rwe
{
    // Every case here comes out of a real game -- the episodes in
    // tad_build_episodes.h, mined from the demo corpus by tad_episodes
    // --emit-build-cpp. Each is one (factory, product) pair of the corpus,
    // consumed as the modal duration of every build of that pair.
    //
    // WHAT IS DELIBERATELY NOT TESTED HERE. Not the factory pipeline. RWE's
    // factory path does not credit build progress on the tick the nanoframe
    // appears: it pushes a unitCreationRequest that spawnNewUnits services
    // later, the following tick starts a StartBuilding COB thread and returns
    // without building, and nothing is credited until the script sets
    // INBUILDSTANCE. The corpus number has the opposite convention and
    // deliberately so -- TA's first increment lands on the 0x09's own tick, and
    // a builder's deploy sequence before INBUILDSTANCE is mod data rather than
    // engine behaviour, which is why tools/tad-buildtime.py scores only immobile
    // builders. So a pipeline-driven measurement would carry RWE's scheduling
    // latency plus whatever a test script did, and compare it against a number
    // chosen to exclude TA's. These cases drive the accumulator directly instead.
    // A pipeline test is a worthwhile separate thing, under a separate name and
    // with no corpus number in it.

    namespace
    {
        /**
         * A unit of the episode's product type, freshly laid down: nothing paid
         * towards its build time, and no world around it, because
         * addBuildProgress reads nothing but the unit and its definition.
         */
        UnitState makeNanoframe(const std::shared_ptr<CobScript>& script)
        {
            std::vector<UnitMesh> pieces;
            UnitState unit(pieces, std::make_unique<CobEnvironment>(script.get()));
            unit.buildTimeCompleted = 0u;
            unit.hitPoints = 0u;
            return unit;
        }

        UnitDefinition productOf(const TadBuildEpisode& episode)
        {
            UnitDefinition def{};
            def.maxHitPoints = 100u;
            def.buildTime = episode.buildTime;
            return def;
        }

        /**
         * The builder's contribution a tick. Integer division, matching
         * workerTimePerTick in src/rwe/LoadingScene_util.cpp and the demo
         * tooling -- they agree, and neither is to be "fixed".
         */
        unsigned int workerTimePerTick(const TadBuildEpisode& episode)
        {
            return episode.workerTime / 30u;
        }

        /** How many calls it takes to finish, which is the duration plus one. */
        unsigned int incrementsToFinish(const TadBuildEpisode& episode)
        {
            auto def = productOf(episode);
            auto script = makeEmptyCobScript();
            auto unit = makeNanoframe(script);

            auto contribution = workerTimePerTick(episode);
            unsigned int increments = 0;
            while (increments < 400000u)
            {
                ++increments;
                if (unit.addBuildProgress(def, contribution))
                {
                    return increments;
                }
            }

            return 0;
        }

        std::string episodeName(const TadBuildEpisode& episode)
        {
            return std::string(episode.builderName) + " -> " + episode.productName
                + " (" + episode.demo + " tick " + std::to_string(episode.startTick)
                + "-" + std::to_string(episode.finishTick) + ")";
        }
    }

    TEST_CASE("a factory build takes as long as it did in a real game", "[build][corpus]")
    {
        // The first increment lands on the tick the nanoframe appears, so a
        // duration is one less than the number of increments -- which is what
        // makes this comparable with a demo's finishTick - startTick at all.
        //
        // Sixteen of these episodes are pairs whose BuildTime divides exactly by
        // the builder's rate, and those are where RWE's integer accumulator and
        // the original's float32 fraction part company: ten of the sixteen need
        // one increment more than the division says, and RWE finishes those a
        // tick early. That is expectedDurationDelta, and section 88 of
        // docs/TOTALA-EXE.md is why it is kept rather than closed.
        for (const auto& episode : tadBuildEpisodes)
        {
            DYNAMIC_SECTION(episodeName(episode))
            {
                auto increments = incrementsToFinish(episode);
                REQUIRE(increments > 0u);

                auto duration = static_cast<int>(increments) - 1;
                REQUIRE(duration
                    == static_cast<int>(episode.modeDurationTicks) + episode.expectedDurationDelta);
            }
        }
    }

    TEST_CASE("a build finishes exactly once, on the increment that completes it", "[build][corpus]")
    {
        // The other half of the count: addBuildProgress has to report done on
        // the increment that fills the last of the build time and on no earlier
        // one, or the duration above would be measuring a clamp rather than the
        // accumulator. It also clamps the contribution to what is left, so the
        // completed total lands exactly on BuildTime and never past it.
        for (const auto& episode : tadBuildEpisodes)
        {
            DYNAMIC_SECTION(episodeName(episode))
            {
                auto def = productOf(episode);
                auto script = makeEmptyCobScript();
                auto unit = makeNanoframe(script);

                auto contribution = workerTimePerTick(episode);
                auto increments = incrementsToFinish(episode);

                for (unsigned int i = 0; i + 1 < increments; ++i)
                {
                    REQUIRE_FALSE(unit.addBuildProgress(def, contribution));
                }

                REQUIRE(unit.buildTimeCompleted < episode.buildTime);
                REQUIRE(unit.addBuildProgress(def, contribution));
                REQUIRE(unit.buildTimeCompleted == episode.buildTime);
                REQUIRE(unit.hitPoints == def.maxHitPoints);
            }
        }
    }

    TEST_CASE("the corpus really does pin a rate and not just a total", "[build][corpus]")
    {
        // A test that only ever divided BuildTime by the rate would pass just as
        // well with the rate wrong and the total wrong by the same factor. The
        // corpus has two rates in it -- WorkerTime 120 and 300, which is p of 4
        // and 10 -- and eleven distinct factories, so halving either number moves
        // the duration and this says so.
        std::set<unsigned int> rates;
        std::set<std::string> builders;
        for (const auto& episode : tadBuildEpisodes)
        {
            rates.insert(workerTimePerTick(episode));
            builders.insert(episode.builderName);
        }

        REQUIRE(rates.size() >= 2u);
        REQUIRE(builders.size() >= 8u);

        // And the deltas are on the divisible pairs and nowhere else, which is
        // the shape section 88 describes rather than a per-episode fudge.
        for (const auto& episode : tadBuildEpisodes)
        {
            auto divisible = episode.buildTime % workerTimePerTick(episode) == 0u;
            if (episode.expectedDurationDelta != 0)
            {
                REQUIRE(episode.expectedDurationDelta == -1);
                REQUIRE(divisible);
                REQUIRE(episode.expectedDifference != nullptr);
            }
            else
            {
                REQUIRE(episode.expectedDifference == nullptr);
            }
        }
    }
}
