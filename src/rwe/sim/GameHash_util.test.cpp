#include <catch2/catch_test_macros.hpp>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/OpaqueId_io.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>

namespace rwe
{
    struct IdTag;
    using Id = OpaqueId<unsigned int, IdTag>;

    enum class TestEnum
    {
        CaseA,
        CaseB,
        CaseC
    };

    using TestVariant = std::variant<int, bool, float>;

    TEST_CASE("computeHashOf")
    {
        SECTION("works on GameHash")
        {
            REQUIRE(computeHashOf(GameHash(1236)) == GameHash(1236));
        }
        SECTION("works on float")
        {
            REQUIRE(computeHashOf(5.0f) == GameHash(327680));
        }
        SECTION("works on bool")
        {
            REQUIRE(computeHashOf(false) == GameHash(0));
            REQUIRE(computeHashOf(true) == GameHash(1));
        }
        SECTION("works on unsigned int")
        {
            REQUIRE(computeHashOf(1234u) == GameHash(1234));
        }
        SECTION("works on int")
        {
            REQUIRE(computeHashOf(1234) == GameHash(1234));
            REQUIRE(computeHashOf(-50) == GameHash(4294967246));
        }
        SECTION("works on string")
        {
            REQUIRE(computeHashOf(std::string("A")) == GameHash(65));
            REQUIRE(computeHashOf(std::string("fred")) == GameHash(417));
        }
        SECTION("works on char*")
        {
            REQUIRE(computeHashOf("A") == GameHash(65));
            REQUIRE(computeHashOf("fred") == GameHash(417));
        }
        SECTION("works on optional")
        {
            REQUIRE(computeHashOf(std::make_optional(38)) == GameHash(38));
            REQUIRE(computeHashOf(std::optional<int>()) == GameHash(0));
        }
        SECTION("works on vector")
        {
            std::vector<int> v{1, 2, 3, 4};
            REQUIRE(computeHashOf(v) == GameHash(10));
        }
        SECTION("works on pair")
        {
            std::pair<int, int> v{1, 2};
            REQUIRE(computeHashOf(v) == GameHash(3));
        }
        SECTION("works on VectorMap")
        {
            VectorMap<int, IdTag> v;
            v.emplace(1);
            v.emplace(2);
            v.emplace(3);
            REQUIRE(computeHashOf(v) == GameHash(774));
        }
        SECTION("works on enum")
        {
            REQUIRE(computeHashOf(TestEnum::CaseA) == GameHash(0));
            REQUIRE(computeHashOf(TestEnum::CaseB) == GameHash(1));
            REQUIRE(computeHashOf(TestEnum::CaseC) == GameHash(2));
        }
        SECTION("works on variant")
        {
            REQUIRE(computeHashOf(TestVariant(12)) == GameHash(12));
            REQUIRE(computeHashOf(TestVariant(true)) == GameHash(2));
            REQUIRE(computeHashOf(TestVariant(1.0f)) == GameHash(65538));
        }
    }

    TEST_CASE("combineHashes")
    {
        SECTION("combines hashes")
        {
            auto hash = combineHashes(GameHash(5), GameHash(6));
            REQUIRE(hash == GameHash(11));
        }

        SECTION("hashes arbitrary items before combining")
        {
            auto hash = combineHashes(true, 25, GameHash(4));
            REQUIRE(hash == GameHash(30));
        }
    }

    namespace
    {
        /**
         * Builds a unit in memory filled with the given byte and hashes it.
         *
         * UnitState's constructor names three members and leaves every other
         * one to its default member initialiser, so a member that has none
         * keeps whatever was already lying in the storage the unit was built
         * in. Filling that storage first is the only way to see it: a fresh
         * page from the operating system arrives zeroed, so such a member
         * reads as zero for as long as the heap is young and stops doing so
         * once it has a history.
         */
        GameHash hashOfNewUnitBuiltInMemoryFilledWith(unsigned char fill)
        {
            auto* storage = ::operator new(sizeof(UnitState), std::align_val_t{alignof(UnitState)});
            std::memset(storage, fill, sizeof(UnitState));

            // Every member of class type is constructed over the fill by the
            // constructor, so only the plain ones can still be holding it,
            // and destroying the unit afterwards is safe.
            std::vector<UnitMesh> pieces;
            auto* unit = new (storage) UnitState(pieces, std::unique_ptr<CobEnvironment>());
            auto hash = computeHashOf(*unit);
            unit->~UnitState();
            ::operator delete(storage, std::align_val_t{alignof(UnitState)});
            return hash;
        }
    }

    TEST_CASE("a new unit hashes the same wherever in memory it was built")
    {
        // A desync with nothing in the simulation to explain it. A member of
        // UnitState that is hashed but has no initialiser is read out of
        // whatever the allocator handed over, so two peers -- or a replay
        // keyframe and the recording it is checked against -- can disagree
        // about a unit that has done nothing yet, and only once the process
        // has allocated enough for the two to land on different rubbish.
        // nanoPoint was such a member: it is the nanolathe nozzle's position,
        // meaningless until the nozzle is first asked for, and hashed from
        // the moment the unit exists.
        //
        // This runs the constructor over storage deliberately filled first,
        // which is the whole point; the values it exposes are ones the
        // simulation should never have been reading.
        REQUIRE(hashOfNewUnitBuiltInMemoryFilledWith(0x00) == hashOfNewUnitBuiltInMemoryFilledWith(0xFF));
        REQUIRE(hashOfNewUnitBuiltInMemoryFilledWith(0x00) == hashOfNewUnitBuiltInMemoryFilledWith(0x5A));
        REQUIRE(hashOfNewUnitBuiltInMemoryFilledWith(0xA5) == hashOfNewUnitBuiltInMemoryFilledWith(0x3C));
    }

    TEST_CASE("the order queue is hashed, and its order matters")
    {
        // Capture progress lives on CaptureOrder rather than on the unit,
        // because that is where the original keeps it (section 96) -- which
        // put it out of the hash's sight until the queue itself was hashed.
        // These pin the two properties that makes it worth having.
        auto move = UnitOrder(MoveOrder(SimVector(1_ss, 2_ss, 3_ss)));
        auto attack = UnitOrder(AttackOrder(UnitId(7)));

        SECTION("an empty queue hashes to nothing")
        {
            std::deque<UnitOrder> empty;
            REQUIRE(computeHashOf(empty) == GameHash(0));
        }

        SECTION("a queue differs from the empty one")
        {
            std::deque<UnitOrder> one{move};
            REQUIRE(computeHashOf(one) != GameHash(0));
        }

        SECTION("reordering changes the hash")
        {
            // The other container helpers fold with a plain sum and could not
            // tell these apart; an order queue's sequence is its meaning.
            std::deque<UnitOrder> forwards{move, attack};
            std::deque<UnitOrder> backwards{attack, move};
            REQUIRE(computeHashOf(forwards) != computeHashOf(backwards));
        }

        SECTION("capture progress reaches the hash")
        {
            auto a = CaptureOrder(UnitId(3));
            auto b = CaptureOrder(UnitId(3));
            b.progress = 42;

            REQUIRE(computeHashOf(a) != computeHashOf(b));

            // And the total, which is snapshotted once and never revisited.
            auto c = CaptureOrder(UnitId(3));
            c.totalWork = 900;
            REQUIRE(computeHashOf(a) != computeHashOf(c));
        }
    }
}
