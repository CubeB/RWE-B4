#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <rwe/game/dump_util.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <rwe/util/OpaqueId_io.h>

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
            u.heldByMission = true;
            u.immune = true;
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
            u.repeatBuildQueue = true;

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

    TEST_CASE("every field a feature's hash reads is one the dump writes")
    {
        // The simulation's hash walks the feature list, so a burning tree
        // whose clock has drifted, or a wreck one peer has reclaimed further
        // than another, desyncs the game. Until dumpJson(const MapFeature&)
        // existed the dump wrote players, units and projectiles and nothing
        // about features at all, so that entire class of mismatch arrived
        // with an empty diff and the hunt stopped there. Issue #115.
        MapFeature f;
        f.featureName = FeatureDefinitionId(7);
        f.position = SimVector(1_ss, 2_ss, 3_ss);
        f.rotation = SimAngle(1234);
        f.velocity = SimVector(4_ss, 5_ss, 6_ss);
        f.reclaimProgress = 42;
        f.hitPoints = 99;
        f.burningUntil = GameTime(900);
        f.animation = FeatureAnimation::Burn;
        f.nextSpark = GameTime(30);
        f.animationTicksRemaining = 171;
        f.burnSparkTicksRemaining = 75;
        f.animationOrder = 3;

        auto dumped = dumpJson(f);
        for (const auto& key : {"featureName", "position", "rotation", "velocity", "reclaimProgress", "hitPoints", "burningUntil", "nextSpark", "animation", "animationTicksRemaining", "burnSparkTicksRemaining", "animationOrder"})
        {
            CAPTURE(key);
            REQUIRE(dumped.contains(key));
        }
        REQUIRE(dumped["reclaimProgress"] == 42);
        REQUIRE(dumped["hitPoints"] == 99);
        REQUIRE(!dumped["burningUntil"].is_null());
    }


    TEST_CASE("the simulation dump carries the features its hash reads")
    {
        // The guard above only says a feature can be dumped. This one says
        // the simulation actually dumps them, which is the half that was
        // missing: dumpJson(const GameSimulation&) wrote players, units and
        // projectiles while computeHashOf read those three and the features.
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);

        FeatureDefinition def{};
        def.name = "rock";
        def.footprintX = 1;
        def.footprintZ = 1;
        def.height = 1_ss;
        def.damage = 55u;
        auto defId = sim.featureDefinitions.insert(def);
        REQUIRE(sim.addFeature(defId, 4, 4).has_value());

        auto dumped = dumpJson(sim);
        REQUIRE(dumped.contains("features"));
        REQUIRE(dumped["features"].size() == 1);
        // A VectorMap dumps as id/value pairs, the same shape units and
        // projectiles come out in, so the feature is under "second".
        const auto& entry = dumped["features"][0]["second"];
        REQUIRE(entry.contains("burningUntil"));
        REQUIRE(entry.contains("reclaimProgress"));
        // addFeature seeds a feature's hit points from its definition's
        // damage, so this is the one field whose value the test can name.
        REQUIRE(entry["hitPoints"] == 55);
    }

    TEST_CASE("a feature hashes by its definition's name, not by its place in the table")
    {
        // Two peers on the same map with different extra content number the
        // same wreck differently, and the hash used to read the number. It
        // desynced at tick 1 with every other field equal.
        const auto build = [](bool otherFirst) {
            auto sim = std::make_unique<GameSimulation>(makeFlatTerrain(32, 32), 0u, 0, 0);

            FeatureDefinition other{};
            other.name = "something_this_machine_also_has";
            other.footprintX = 1;
            other.footprintZ = 1;
            other.height = 1_ss;

            FeatureDefinition rock{};
            rock.name = otherFirst ? "ROCK" : "rock";
            rock.footprintX = 1;
            rock.footprintZ = 1;
            rock.height = 1_ss;
            rock.damage = 55u;

            FeatureDefinitionId rockId;
            if (otherFirst)
            {
                sim->featureDefinitions.insert(other);
                rockId = sim->featureDefinitions.insert(rock);
            }
            else
            {
                rockId = sim->featureDefinitions.insert(rock);
                sim->featureDefinitions.insert(other);
            }
            REQUIRE(sim->addFeature(rockId, 4, 4).has_value());
            return sim;
        };

        auto a = build(false);
        auto b = build(true);
        REQUIRE(a->features.begin()->second.featureName != b->features.begin()->second.featureName);
        REQUIRE(computeHashOf(*a) == computeHashOf(*b));

        // And the name is read: a different feature in the same place is a different game.
        auto c = build(false);
        FeatureDefinition tree{};
        tree.name = "tree";
        tree.footprintX = 1;
        tree.footprintZ = 1;
        tree.height = 1_ss;
        tree.damage = 55u;
        auto treeId = c->featureDefinitions.insert(tree);
        c->features.begin()->second.featureName = treeId;
        REQUIRE(computeHashOf(*a) != computeHashOf(*c));
    }

    TEST_CASE("feature name casing does not cause a tick-one desync", "[hash]")
    {
        // The two 1a0848cd reports differed by exactly 384: these two names
        // each contributed six lowercase ASCII letters, worth 6 * 32.
        MapFeature feature{};
        feature.position = SimVector(0_ss, 0_ss, 0_ss);
        REQUIRE(computeHashOf(feature, "Tree1Dead") == computeHashOf(feature, "TREE1DEAD"));
        REQUIRE(computeHashOf(feature, "Tree2Dead") == computeHashOf(feature, "TREE2DEAD"));
        REQUIRE(computeHashOf(feature, "Tree1Dead") != computeHashOf(feature, "TREE2DEAD"));
    }

    TEST_CASE("the simulation dump names the definitions its feature numbers stand for")
    {
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        FeatureDefinition def{};
        def.name = "rock";
        def.footprintX = 1;
        def.footprintZ = 1;
        def.height = 1_ss;
        sim.featureDefinitions.insert(def);

        auto dumped = dumpJson(sim);
        REQUIRE(dumped.contains("featureDefinitions"));
        REQUIRE(dumped["featureDefinitions"].size() == 1);
        REQUIRE(dumped["featureDefinitions"][0]["name"] == "rock");
    }

    TEST_CASE("every field a projectile's hash reads is one the dump writes")
    {
        // The same fault as the features one, a field at a time rather than a
        // whole type: edgeEffectiveness went into the sync hash and not into
        // the dump, so a round that disagreed about its falloff showed up as
        // a mismatch the dump could not account for. Issue #115.
        //
        // The list is what computeHashOf(const Projectile&) reads, in its
        // order. Adding to one means adding to the other.
        Projectile p{};
        auto dumped = dumpJson(p);
        for (const auto& key : {"owner", "attacker", "position", "origin", "velocity", "damageRadius", "edgeEffectiveness", "heading", "pitch", "speed", "secondPhase", "targetProjectile", "motorOut"})
        {
            CAPTURE(key);
            REQUIRE(dumped.contains(key));
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

    TEST_CASE("two factories differing only in the repeat toggle hash differently", "[hash]")
    {
        // The factory repeat flag (#417) decides whether a factory consumes
        // its queue or loops it, so two peers that disagree about it build
        // different units from the same queue and diverge from that tick on.
        // It is hashed for the same reason the order queue is: a player-set
        // decision about what the simulation does next, not a presentation
        // detail.
        auto off = makePopulatedUnitState();
        off.repeatBuildQueue = false;

        auto on = makePopulatedUnitState();
        on.repeatBuildQueue = true;

        REQUIRE(computeHashOf(off) != computeHashOf(on));
    }

    TEST_CASE("a fully-populated unit keeps its pinned hash value")
    {
        // The hash, save and dump walks are derived from one field table, so
        // a slip in the table moves the bytes. This is the pin: the value is
        // what the pre-table hand-written hash produced for this unit, and
        // the walks must agree with it from now on. It was 469200823 until
        // 2026-09-24, when reclaimProgress left the table (#19: a unit is
        // reclaimed through its hit points now, and had nothing to keep here),
        // and then 469200811 until heldByMission and immune joined it (#38),
        // adding one each. repeatBuildQueue joined it on 2026-09-28 (#417),
        // adding one more.
        REQUIRE(computeHashOf(makePopulatedUnitState()) == GameHash(469200814u));
    }

    TEST_CASE("a player's sharing flags and reserves are hashed", "[hash]")
    {
        // Resource sharing (TOTALA-EXE-COMMANDS.md section 124) moves stock
        // between players, so two peers that disagree about a toggle or a
        // reserve diverge at the next automatic pass. Each of the four is
        // checked alone, so a slip that drops one names which.
        auto make = []() {
            return GamePlayerInfo{
                std::optional<std::string>("p"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(100.0f),
                Energy(100.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(100.0f),
                Energy(100.0f),
            };
        };

        auto base = computeHashOf(make());

        auto shareMetal = make();
        shareMetal.shareMetal = true;
        REQUIRE(computeHashOf(shareMetal) != base);

        auto shareEnergy = make();
        shareEnergy.shareEnergy = true;
        REQUIRE(computeHashOf(shareEnergy) != base);
        REQUIRE(computeHashOf(shareEnergy) != computeHashOf(shareMetal));

        auto metalReserve = make();
        metalReserve.metalReserve = Metal(250.0f);
        REQUIRE(computeHashOf(metalReserve) != base);

        auto energyReserve = make();
        energyReserve.energyReserve = Energy(250.0f);
        REQUIRE(computeHashOf(energyReserve) != base);

        // The hash is a sum, so equal values in the two accounts must not be
        // interchangeable in it.
        REQUIRE(computeHashOf(metalReserve) != computeHashOf(energyReserve));
    }

    TEST_CASE("a new player's sharing state is the same wherever in memory it was built", "[hash]")
    {
        // The hash reads these from the moment the player exists, so each
        // has to come up initialised: filled storage, built over twice with
        // different bytes, must hash the same as a default.
        auto hashOverFill = [](unsigned char fill) {
            alignas(GamePlayerInfo) unsigned char storage[sizeof(GamePlayerInfo)];
            std::memset(storage, fill, sizeof(storage));
            auto* p = new (storage) GamePlayerInfo{
                std::optional<std::string>("p"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(100.0f),
                Energy(100.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(100.0f),
                Energy(100.0f),
            };
            auto hash = computeHashOf(*p);
            p->~GamePlayerInfo();
            return hash;
        };

        REQUIRE(hashOverFill(0x00) == hashOverFill(0xAB));
    }


    TEST_CASE("the ported navigation timers and repair leash affect the sync hash", "[porting][hash]")
    {
        UnitState::AirLoiterState leg;
        leg.anchor = SimVector(0_ss, 0_ss, 0_ss);
        auto baseline = computeHashOf(leg);
        leg.waypoint.x = 1_ss;
        REQUIRE(computeHashOf(leg) != baseline);
        leg.waypoint.x = 0_ss;
        leg.dueAt = GameTime(1);
        REQUIRE(computeHashOf(leg) != baseline);
        RepairOrder repair(UnitId(1));
        auto repairHash = computeHashOf(repair);
        repair.leash = AttackLeash(SimVector(0_ss, 0_ss, 0_ss), 1_ss);
        REQUIRE(computeHashOf(repair) != repairHash);
        GamePlayerInfo player{std::nullopt, GamePlayerType::Human, PlayerColorIndex(0), GamePlayerStatus::Alive, "ARM", Metal(0), Energy(0), Metal(0), Energy(0), Metal(0), Energy(0)};
        auto playerHash = computeHashOf(player);
        player.airBasesRebuiltAt = GameTime(1);
        REQUIRE(computeHashOf(player) != playerHash);
    }
}
