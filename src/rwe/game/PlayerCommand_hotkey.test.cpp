#include <catch2/catch_test_macros.hpp>
#include <rwe/game/PlayerCommand.h>
#include <rwe/proto/serialization.h>
#include <rwe/sim/UnitId.h>
#include <unordered_set>
#include <variant>

// Tests for the hotkey-introduced PlayerUnitCommand::SelfDestruct variant
// and the control-group set-operation logic (which lives in GameScene but
// is expressed purely in terms of std::unordered_set, so we can test it here
// without instantiating the full scene).

namespace rwe
{
    // ---------------------------------------------------------------------------
    // SelfDestruct command: protobuf round-trip
    // ---------------------------------------------------------------------------

    TEST_CASE("SelfDestruct command protobuf round-trip", "[hotkey]")
    {
        SECTION("serialises to self_destruct oneof field")
        {
            UnitId uid{42};
            PlayerCommand original = PlayerUnitCommand(uid, PlayerUnitCommand::SelfDestruct());
            proto::PlayerCommand wire;
            serializePlayerCommand(original, wire);

            REQUIRE(wire.has_unit_command());
            REQUIRE(wire.unit_command().unit() == 42);
            REQUIRE(wire.unit_command().has_self_destruct());
        }

        SECTION("deserialises back to SelfDestruct variant")
        {
            UnitId uid{7};
            PlayerCommand original = PlayerUnitCommand(uid, PlayerUnitCommand::SelfDestruct());
            proto::PlayerCommand wire;
            serializePlayerCommand(original, wire);

            PlayerCommand restored = deserializeCommand(wire);
            REQUIRE(std::holds_alternative<PlayerUnitCommand>(restored));
            const auto& uc = std::get<PlayerUnitCommand>(restored);
            REQUIRE(uc.unit == uid);
            REQUIRE(std::holds_alternative<PlayerUnitCommand::SelfDestruct>(uc.command));
        }

        SECTION("self_destruct does not conflict with existing Stop variant")
        {
            UnitId uid{1};
            PlayerCommand stopCmd = PlayerUnitCommand(uid, PlayerUnitCommand::Stop());
            proto::PlayerCommand wireStop;
            serializePlayerCommand(stopCmd, wireStop);
            REQUIRE(wireStop.unit_command().has_stop());
            REQUIRE_FALSE(wireStop.unit_command().has_self_destruct());

            PlayerCommand sdCmd = PlayerUnitCommand(uid, PlayerUnitCommand::SelfDestruct());
            proto::PlayerCommand wireSd;
            serializePlayerCommand(sdCmd, wireSd);
            REQUIRE(wireSd.unit_command().has_self_destruct());
            REQUIRE_FALSE(wireSd.unit_command().has_stop());
        }
    }

    // ---------------------------------------------------------------------------
    // Control-group set-operation logic
    // These replicate exactly what GameScene::onKeyDown does for groups so
    // that the business rules are independently verifiable.
    // ---------------------------------------------------------------------------

    // Bind (Ctrl+digit): replace group with current selection.
    static void bindGroup(std::unordered_set<UnitId>& group, const std::unordered_set<UnitId>& selection)
    {
        group = selection;
    }

    // Add (Shift+digit or Ctrl+Shift+digit): union group with current selection.
    static void addToGroup(std::unordered_set<UnitId>& group, const std::unordered_set<UnitId>& selection)
    {
        for (const auto& id : selection)
        {
            group.insert(id);
        }
    }

    // Recall (digit alone): return live members, pruning dead ones.
    // In production this calls tryGetUnit; here we pass a predicate for liveness.
    static std::unordered_set<UnitId> recallGroup(
        std::unordered_set<UnitId>& group,
        const std::unordered_set<UnitId>& liveUnits)
    {
        std::unordered_set<UnitId> result;
        for (const auto& id : group)
        {
            if (liveUnits.count(id))
            {
                result.insert(id);
            }
        }
        group = result; // prune dead entries from stored group
        return result;
    }

    TEST_CASE("Control group bind replaces group contents", "[hotkey]")
    {
        std::unordered_set<UnitId> group{UnitId{1}, UnitId{2}};
        std::unordered_set<UnitId> selection{UnitId{3}, UnitId{4}};

        bindGroup(group, selection);

        REQUIRE(group == selection);
        REQUIRE_FALSE(group.count(UnitId{1}));
        REQUIRE_FALSE(group.count(UnitId{2}));
    }

    TEST_CASE("Control group bind with empty selection clears group", "[hotkey]")
    {
        std::unordered_set<UnitId> group{UnitId{1}};
        std::unordered_set<UnitId> empty;

        bindGroup(group, empty);

        REQUIRE(group.empty());
    }

    TEST_CASE("Control group add is a union", "[hotkey]")
    {
        std::unordered_set<UnitId> group{UnitId{1}, UnitId{2}};
        std::unordered_set<UnitId> selection{UnitId{2}, UnitId{3}};

        addToGroup(group, selection);

        // Union: 1, 2, 3
        REQUIRE(group.size() == 3);
        REQUIRE(group.count(UnitId{1}));
        REQUIRE(group.count(UnitId{2}));
        REQUIRE(group.count(UnitId{3}));
    }

    TEST_CASE("Control group add from empty selection is a no-op", "[hotkey]")
    {
        std::unordered_set<UnitId> group{UnitId{5}};
        std::unordered_set<UnitId> empty;

        addToGroup(group, empty);

        REQUIRE(group.size() == 1);
        REQUIRE(group.count(UnitId{5}));
    }

    TEST_CASE("Control group recall returns only live units", "[hotkey]")
    {
        std::unordered_set<UnitId> group{UnitId{1}, UnitId{2}, UnitId{3}};
        // Units 1 and 3 are alive; 2 is dead.
        std::unordered_set<UnitId> live{UnitId{1}, UnitId{3}};

        auto recalled = recallGroup(group, live);

        REQUIRE(recalled == live);
        // Dead unit must be pruned from the stored group.
        REQUIRE_FALSE(group.count(UnitId{2}));
    }

    TEST_CASE("Control group recall with all dead units returns empty set", "[hotkey]")
    {
        std::unordered_set<UnitId> group{UnitId{10}, UnitId{11}};
        std::unordered_set<UnitId> live; // nothing alive

        auto recalled = recallGroup(group, live);

        REQUIRE(recalled.empty());
        REQUIRE(group.empty());
    }

    TEST_CASE("Control group recall with empty group returns empty set", "[hotkey]")
    {
        std::unordered_set<UnitId> group;
        std::unordered_set<UnitId> live{UnitId{1}};

        auto recalled = recallGroup(group, live);

        REQUIRE(recalled.empty());
    }

    TEST_CASE("Bind then recall round-trips the selection", "[hotkey]")
    {
        std::unordered_set<UnitId> group;
        std::unordered_set<UnitId> selection{UnitId{7}, UnitId{8}};
        std::unordered_set<UnitId> live = selection; // all alive

        bindGroup(group, selection);
        auto recalled = recallGroup(group, live);

        REQUIRE(recalled == selection);
    }
}
