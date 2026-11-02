#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <rwe/mod/ModStateStore.h>

namespace rwe
{
    TEST_CASE("the mod store keeps global, player and unit scopes apart", "[modsdk][modstore]")
    {
        ModStateStore store;
        CHECK(store.setGlobal("a.mod", "counter", ModValue::integer(3)));
        CHECK(store.setPlayer("a.mod", PlayerId(1), "metal", ModValue::number(4.5f)));
        CHECK(store.setUnit("a.mod", UnitId(9), "shield", ModValue::integer(100)));

        const auto counter = store.getGlobal("a.mod", "counter");
        REQUIRE(counter);
        CHECK(counter->type == ModValueType::I32);
        CHECK(counter->i32 == 3);

        REQUIRE(store.getPlayer("a.mod", PlayerId(1), "metal"));
        CHECK(store.getPlayer("a.mod", PlayerId(1), "metal")->f32 == 4.5f);
        CHECK_FALSE(store.getPlayer("a.mod", PlayerId(2), "metal"));
        CHECK_FALSE(store.getPlayer("a.mod", PlayerId(1), "counter"));

        REQUIRE(store.getUnit("a.mod", UnitId(9), "shield"));
        CHECK(store.getUnit("a.mod", UnitId(9), "shield")->i32 == 100);
        CHECK_FALSE(store.getUnit("a.mod", UnitId(8), "shield"));
        CHECK_FALSE(store.getGlobal("a.mod", "shield"));
    }

    TEST_CASE("one mod writes its namespace and may read another's", "[modsdk][modstore]")
    {
        ModStateStore store;
        CHECK(store.setGlobal("a.mod", "shared", ModValue::text("hello")));
        CHECK(store.setGlobal("b.mod", "own", ModValue::binary(std::string("\x00\x01\xff", 3))));

        REQUIRE(store.getGlobal("a.mod", "shared"));
        CHECK(store.getGlobal("a.mod", "shared")->bytes == "hello");
        CHECK_FALSE(store.getGlobal("b.mod", "shared"));
        REQUIRE(store.getGlobal("b.mod", "own"));
        CHECK(store.getGlobal("b.mod", "own")->bytes == std::string("\x00\x01\xff", 3));
    }

    TEST_CASE("the store enforces per-mod quotas and a refused write changes nothing", "[modsdk][modstore]")
    {
        ModStateLimits limits;
        limits.maxEntriesPerMod = 2;
        limits.maxBytesPerMod = 64;
        ModStateStore store(limits);

        CHECK(store.setGlobal("a.mod", "one", ModValue::integer(1)));
        CHECK(store.setGlobal("a.mod", "two", ModValue::integer(2)));
        const auto before = store.hash();
        CHECK_FALSE(store.setGlobal("a.mod", "three", ModValue::integer(3)));
        CHECK(store.hash() == before);
        CHECK(store.entriesFor("a.mod") == 2);
        CHECK_FALSE(store.getGlobal("a.mod", "three"));

        // Another mod has its own budget.
        CHECK(store.setGlobal("b.mod", "one", ModValue::integer(1)));

        // Replacing a key is not a new entry.
        CHECK(store.setGlobal("a.mod", "one", ModValue::integer(7)));
        CHECK(store.entriesFor("a.mod") == 2);
        CHECK(store.getGlobal("a.mod", "one")->i32 == 7);
    }

    TEST_CASE("a unit's and a player's entries are deleted with them", "[modsdk][modstore]")
    {
        ModStateStore store;
        CHECK(store.setUnit("a.mod", UnitId(1), "charge", ModValue::integer(5)));
        CHECK(store.setUnit("b.mod", UnitId(1), "charge", ModValue::integer(6)));
        CHECK(store.setUnit("a.mod", UnitId(2), "charge", ModValue::integer(7)));
        CHECK(store.setPlayer("a.mod", PlayerId(3), "income", ModValue::number(2.0f)));

        const auto before = store.hash();
        store.removeUnit(UnitId(1));
        CHECK_FALSE(store.getUnit("a.mod", UnitId(1), "charge"));
        CHECK_FALSE(store.getUnit("b.mod", UnitId(1), "charge"));
        REQUIRE(store.getUnit("a.mod", UnitId(2), "charge"));
        CHECK(store.hash() != before);

        store.removePlayer(PlayerId(3));
        CHECK_FALSE(store.getPlayer("a.mod", PlayerId(3), "income"));

        // Removing what is not there is a no-op.
        const auto afterRemovals = store.hash();
        store.removeUnit(UnitId(1));
        store.removePlayer(PlayerId(3));
        CHECK(store.hash() == afterRemovals);
    }

    TEST_CASE("the store's canonical form round-trips and hashes by content", "[modsdk][modstore]")
    {
        ModStateStore first;
        CHECK(first.setGlobal("a.mod", "int", ModValue::integer(-4)));
        CHECK(first.setGlobal("a.mod", "float", ModValue::number(1.25f)));
        CHECK(first.setGlobal("a.mod", "text", ModValue::text("x")));
        CHECK(first.setGlobal("a.mod", "bytes", ModValue::binary(std::string("\x00\x10", 2))));
        CHECK(first.setPlayer("b.mod", PlayerId(2), "p", ModValue::integer(1)));
        CHECK(first.setUnit("b.mod", UnitId(7), "u", ModValue::integer(2)));

        ModStateStore second;
        // Insertion order must not matter: the store and its hash are sorted.
        CHECK(second.setUnit("b.mod", UnitId(7), "u", ModValue::integer(2)));
        CHECK(second.setGlobal("a.mod", "bytes", ModValue::binary(std::string("\x00\x10", 2))));
        CHECK(second.setPlayer("b.mod", PlayerId(2), "p", ModValue::integer(1)));
        CHECK(second.setGlobal("a.mod", "text", ModValue::text("x")));
        CHECK(second.setGlobal("a.mod", "float", ModValue::number(1.25f)));
        CHECK(second.setGlobal("a.mod", "int", ModValue::integer(-4)));

        CHECK(first.hash() == second.hash());
        CHECK(first.toJson().dump() == second.toJson().dump());
        REQUIRE(first.toJson()["globals"]["a.mod"]["bytes"]["value"] == "0010");

        ModStateStore restored;
        restored.loadJson(first.toJson());
        CHECK(restored.hash() == first.hash());

        CHECK(restored.setGlobal("a.mod", "int", ModValue::integer(-5)));
        CHECK(restored.hash() != first.hash());
    }

    TEST_CASE("malformed store state is refused and leaves the store empty", "[modsdk][modstore]")
    {
        ModStateStore store;
        CHECK(store.setGlobal("a.mod", "x", ModValue::integer(1)));

        // A refused load leaves the current state exactly as it was.
        REQUIRE_THROWS(store.loadJson(nlohmann::json::object()));
        REQUIRE(store.getGlobal("a.mod", "x"));
        CHECK(store.getGlobal("a.mod", "x")->i32 == 1);

        auto value = nlohmann::json{
            {"format", "rwe.mod-state"},
            {"version", 1},
            {"globals", {{"a.mod", {{"x", {{"type", "i32"}, {"value", "not-a-number"}}}}}}},
        };
        REQUIRE_THROWS(store.loadJson(value));
        REQUIRE(store.getGlobal("a.mod", "x"));
        CHECK(store.getGlobal("a.mod", "x")->i32 == 1);

        value["globals"]["a.mod"]["x"] = {{"type", "f32"}, {"value", 0.0}};
        value["globals"]["a.mod"]["x"]["value"] = std::numeric_limits<double>::infinity();
        REQUIRE_THROWS(store.loadJson(value));
        REQUIRE(store.getGlobal("a.mod", "x"));

        // A valid empty state is a real load, not a refusal.
        store.loadJson(nlohmann::json{
            {"format", "rwe.mod-state"},
            {"version", 1},
            {"globals", nlohmann::json::object()},
            {"players", nlohmann::json::object()},
            {"units", nlohmann::json::object()},
        });
        CHECK_FALSE(store.getGlobal("a.mod", "x"));
    }
}
