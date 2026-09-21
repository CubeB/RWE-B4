#include <catch2/catch_test_macros.hpp>
#include <rwe/game/dump_util.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/sim_test_util.h>
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

    namespace
    {
        /**
         * Builds a unit with every member the sync hash reads carrying
         * something, and nothing left at its default.
         *
         * The point of this unit is the pinned-hash test below: the field
         * table that drives the hash, save and dump walks must produce the
         * same bytes the hand-written hash did, and only a unit with
         * everything populated exercises that claim.
         */
        UnitState makePopulatedUnitState()
        {
            static const auto script = makeEmptyCobScript();

            std::vector<UnitMesh> pieces{UnitMesh()};
            UnitState u(pieces, std::make_unique<CobEnvironment>(script.get()));

            u.unitType = "ARMCK";
            u.position = SimVector(1_ss, 2_ss, 3_ss);
            u.owner = PlayerId(2);
            u.rotation = SimAngle(100);
            u.physics = UnitPhysicsInfoGround{
                SteeringInfo{SimAngle(12), SimScalar(3_ss)},
                SimScalar(4_ss)};
            u.hitPoints = 90;
            u.lifeState = UnitState::LifeStateDead{true, 2u};
            u.orders.push_back(MoveOrder(SimVector(3_ss, 4_ss, 5_ss)));
            u.orders.push_back(AttackOrder(UnitId(7), AttackLeash(SimVector(6_ss, 7_ss, 8_ss), SimScalar(2_ss))));
            auto& attack = std::get<AttackOrder>(u.orders.back());
            attack.lastSeenPosition = SimVector(9_ss, 9_ss, 0_ss);
            u.orders.push_back(BuildOrder("ARMSOLAR", SimVector(10_ss, 0_ss, 11_ss)));
            u.orders.push_back(CompleteBuildOrder(UnitId(13)));
            u.orders.push_back(GuardOrder(UnitId(14)));
            u.orders.push_back(ReclaimOrder(FeatureId(15)));
            u.orders.push_back(RepairOrder(UnitId(16)));
            u.orders.push_back(PatrolOrder(SimVector(17_ss, 0_ss, 18_ss)));
            u.orders.push_back(LoadOrder(UnitId(19)));
            u.orders.push_back(UnloadOrder(SimVector(20_ss, 0_ss, 21_ss)));
            std::get<UnloadOrder>(u.orders.back()).parkedUntil = GameTime(123);
            u.orders.push_back(DgunOrder(SimVector(22_ss, 0_ss, 23_ss)));
            u.orders.push_back(LandOnAirBaseOrder(UnitId(24)));
            u.orders.push_back(ResurrectOrder(FeatureId(25)));
            u.orders.push_back(CaptureOrder(UnitId(26)));
            std::get<CaptureOrder>(u.orders.back()).progress = 42u;
            std::get<CaptureOrder>(u.orders.back()).totalWork = 900u;
            u.orders.push_back(BuggerOffOrder(DiscreteRect(1, 2, 3, 4)));
            u.behaviourState = UnitBehaviorStateBuilding{UnitId(11), SimVector(30_ss, 0_ss, 31_ss)};
            u.navigationState = NavigationStateInfo{
                NavigationGoal(SimVector(40_ss, 0_ss, 41_ss)),
                UnitPositionCache{UnitId(5), SimVector(42_ss, 0_ss, 43_ss), GameTime(77)},
                UnitPositionCache{UnitId(6), SimVector(44_ss, 0_ss, 45_ss), GameTime(88)},
                NavigationStateMoving{
                    MovingStateGoal(UnitId(9)),
                    PathDestination(SimVector(46_ss, 0_ss, 47_ss)),
                    std::nullopt,
                    true,
                    SimVector(48_ss, 0_ss, 49_ss)}};
            u.buildOrderUnitId = UnitId(51);
            u.inBuildStance = true;
            u.armStowDueTime = GameTime(1200);
            u.nanoPointQueriedAt = GameTime(52);
            u.nanoPoint = SimVector(53_ss, 54_ss, 55_ss);
            u.yardOpen = true;
            u.inCollision = true;

            UnitWeapon weapon;
            weapon.weaponType = "CORVULC";
            weapon.readyTime = GameTime(56);
            weapon.ballisticZOffset = SimScalar(57_ss);
            weapon.stockedRounds = 3;
            weapon.queuedRounds = 2;
            weapon.stockpileProgress = 9;
            weapon.stockpileStepDelay = 4;
            u.weapons[0] = weapon;
            u.weapons[1] = weapon;

            u.fireOrders = UnitFireOrders::ReturnFire;
            u.moveOrders = UnitMovementOrders::Maneuver;
            u.cobBusy = true;
            u.buggerOffActive = true;
            u.armored = true;
            u.kills = 4;
            u.sfxOccupyState = 3;
            u.buildTimeCompleted = 234;
            u.nanoframeDecayTime = GameTime(4321);
            u.nanoframeWorkedOn = true;
            u.nanoframeDecayRemainder = 7;
            u.reclaimProgress = 12;
            u.selfDestructTime = GameTime(555);
            u.paralyzedUntil = GameTime(666);
            u.moveRateBand = 3;
            u.carriedBy = UnitId(9);
            u.carriedUnits = {UnitId(12), UnitId(17)};
            u.transportScriptTarget = UnitId(10);
            u.transportScriptStartedAt = GameTime(11);
            u.airWorkOrbit = UnitState::AirWorkOrbitState{SimVector(60_ss, 0_ss, 61_ss), SimAngle(62), true};
            u.airLoiter = UnitState::AirLoiterState{UnitState::AirLoiterState::Reason::Guarding, SimVector(63_ss, 0_ss, 64_ss), SimAngle(65)};
            u.slowFacePoint = SimVector(66_ss, 0_ss, 67_ss);
            u.activated = true;
            u.isSufficientlyPowered = true;
            u.cloakRequested = true;
            u.cloaked = true;
            u.cloakSuppressedUntil = GameTime(50);
            u.energyProductionBuffer = Energy{1.5f};
            u.metalProductionBuffer = Metal{2.5f};
            u.previousEnergyProductionBuffer = Energy{3.5f};
            u.previousMetalProductionBuffer = Metal{4.5f};
            u.previousEnergyConsumptionBuffer = Energy{5.5f};
            u.previousMetalConsumptionBuffer = Metal{6.5f};
            u.energyConsumptionBuffer = Energy{7.5f};
            u.metalConsumptionBuffer = Metal{8.5f};
            u.energyRequestBuffer = Energy{9.5f};
            u.metalRequestBuffer = Metal{10.5f};
            u.energyDebt = Energy{11.5f};
            u.metalDebt = Metal{12.5f};
            u.buildQueue.emplace_back("ARMSOLAR", 2);
            u.buildQueue.emplace_back("ARMPNQ", 1);

            return u;
        }
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

    TEST_CASE("a capture's progress reaches the desync dump")
    {
        // Hunting a desync is RWE_HASH_LOG to find the tick and then
        // RWE_STATE_DUMP to bisect to the field, so the second step can only
        // show what the dump walks. Capture progress is hashed and lives on
        // the order rather than on the unit (section 96), so while the order
        // queue went undumped this was a divergence the hunt could not
        // explain. Issue #115.
        auto dumped = dumpJson(makePopulatedUnitState());
        REQUIRE(dumped.contains("orders"));

        auto captures = 0;
        for (const auto& order : dumped["orders"])
        {
            if (!order["data"].contains("progress"))
            {
                continue;
            }
            ++captures;
            REQUIRE(order["data"]["progress"] == 42u);
            REQUIRE(order["data"]["totalWork"] == 900u);
        }
        REQUIRE(captures == 1);
    }

    TEST_CASE("a fully-populated unit keeps its pinned hash value")
    {
        // The hash, save and dump walks are derived from one field table, so
        // a slip in the table moves the bytes. This is the pin: the value is
        // what the pre-table hand-written hash produced for this unit, and
        // the walks must agree with it from now on.
        REQUIRE(computeHashOf(makePopulatedUnitState()) == GameHash(469200823u));
    }

}
