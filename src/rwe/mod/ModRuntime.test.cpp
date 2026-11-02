#include <catch2/catch_test_macros.hpp>
#include <rwe/mod/ModRuntime.h>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        ModRuntimePackage package(const std::string& id, const std::string& source)
        {
            return {id, id + "/scripts/main.lua", source, nlohmann::json::object()};
        }

        struct Fixture
        {
            ModStateStore store;
            ModRuntime runtime;

            Fixture(std::vector<ModRuntimePackage> packages, std::uint32_t seed = 7)
                : runtime(std::move(packages), seed, store)
            {
            }
        };
    }

    TEST_CASE("runtime damage hooks fold in load order", "[modsdk][modruntime]")
    {
        Fixture fixture({
            package("a.double", "function hook_damage_v1(v, a, amount, paralyzer, prev) return prev * 2 end"),
            package("b.plus", "function hook_damage_v1(v, a, amount, paralyzer, prev) return prev + 10 end"),
        });

        CHECK(fixture.runtime.adjustDamage(UnitId(1), UnitId(2), 5.0f, false) == 20.0f);
        CHECK(fixture.runtime.disabledPackages().empty());
    }

    TEST_CASE("a faulty runtime package is disabled and its fold passes through", "[modsdk][modruntime]")
    {
        Fixture fixture({
            package("a.bad", "function hook_damage_v1(v, a, amount, paralyzer, prev) error('boom') end"),
            package("b.double", "function hook_damage_v1(v, a, amount, paralyzer, prev) return prev * 2 end"),
        });

        CHECK(fixture.runtime.adjustDamage(UnitId(1), UnitId(2), 5.0f, false) == 10.0f);
        const auto disabled = fixture.runtime.disabledPackages();
        REQUIRE(disabled.size() == 1);
        CHECK(disabled[0] == "a.bad");
        REQUIRE_FALSE(fixture.runtime.log().empty());
        CHECK(fixture.runtime.log().back().find("a.bad") != std::string::npos);

        // A second call stays disabled and does not re-enter the script.
        CHECK(fixture.runtime.adjustDamage(UnitId(1), UnitId(2), 5.0f, false) == 10.0f);
        CHECK(fixture.runtime.disabledPackages().size() == 1);
    }

    TEST_CASE("a runtime package accumulates state across ticks", "[modsdk][modruntime]")
    {
        Fixture fixture({
            package("a.counter", R"(
                function on_tick(tick)
                    local count = rwe.store_get("count") or 0
                    rwe.store_set("count", count + 1)
                    rwe.log("tick " .. tick)
                end
            )"),
        });

        fixture.runtime.onTick(1);
        fixture.runtime.onTick(2);
        fixture.runtime.onTick(3);

        const auto value = fixture.store.getGlobal("a.counter", "count");
        REQUIRE(value);
        CHECK(value->type == ModValueType::F32);
        CHECK(value->f32 == 3.0f);
        REQUIRE(fixture.runtime.log().size() == 3);
        CHECK(fixture.runtime.log().back() == "tick 3");
    }

    TEST_CASE("runtime randomness is seeded and restored", "[modsdk][modruntime]")
    {
        const auto source = R"(
            function on_tick(tick)
                rwe.store_set("roll", rwe.rand_below(1000))
            end
        )";

        Fixture first({package("a.random", source)});
        first.runtime.onTick(1);
        const auto firstRoll = first.store.getGlobal("a.random", "roll");
        REQUIRE(firstRoll);

        Fixture second({package("a.random", source)});
        second.runtime.onTick(1);
        const auto secondRoll = second.store.getGlobal("a.random", "roll");
        REQUIRE(secondRoll);
        CHECK(firstRoll->f32 == secondRoll->f32);

        // A restored runtime continues the same stream, not a fresh one.
        second.runtime.loadJson(first.runtime.toJson());
        CHECK(second.runtime.hash() == first.runtime.hash());
        first.runtime.onTick(2);
        second.runtime.onTick(2);
        CHECK(first.store.getGlobal("a.random", "roll")->f32
            == second.store.getGlobal("a.random", "roll")->f32);
    }

    TEST_CASE("runtime metadata round-trips and hashes by content", "[modsdk][modruntime]")
    {
        Fixture fixture({
            package("a.state", R"(
                function on_tick(tick)
                    rwe.store_set("value", tick * 2)
                end
            )"),
            package("a.bad", "function hook_damage_v1() error('fault') end"),
        });

        fixture.runtime.onTick(1);
        fixture.runtime.adjustDamage(UnitId(1), UnitId(2), 1.0f, false);
        REQUIRE(fixture.runtime.disabledPackages().size() == 1);

        const auto saved = fixture.runtime.toJson();
        CHECK(saved["format"] == "rwe.mod-runtime");
        CHECK(saved["version"] == 1);
        CHECK(saved["disabled"][0] == "a.bad");
        REQUIRE(saved["rng"].size() == 2);
        CHECK(saved.contains("store"));

        ModStateStore restoredStore;
        ModRuntime restored(
            {package("a.state", R"(
                function on_tick(tick)
                    rwe.store_set("value", tick * 2)
                end
            )"),
                package("a.bad", "function hook_damage_v1() error('fault') end")},
            7,
            restoredStore);
        restored.loadJson(saved);
        CHECK(restored.hash() == fixture.runtime.hash());
        CHECK(restored.disabledPackages().size() == 1);
        REQUIRE(restoredStore.getGlobal("a.state", "value"));
        CHECK(restoredStore.getGlobal("a.state", "value")->f32 == 2.0f);
    }

    TEST_CASE("runtime scripts have no outside world", "[modsdk][modruntime]")
    {
        Fixture fixture({
            package("a.sandbox", R"(
                assert(os == nil)
                assert(io == nil)
                assert(package == nil)
                assert(debug == nil)
                assert(load == nil and dofile == nil and loadfile == nil)
                assert(math.random == nil and math.randomseed == nil)
                assert(rwe.api_version == 1)
            )"),
        });
        fixture.runtime.onTick(1);
        CHECK(fixture.runtime.disabledPackages().empty());
    }
}
