#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <rwe/sim/sim_test_util.h>

/**
 * Resurrect: turning a corpse back into what it came from.
 *
 * Row 42 of the ground mission table, handler `0x404DB0`. Nothing in the
 * shipped data can issue it -- not one of the 189 FBIs names `CanResurrect` --
 * so every number here comes from the decode in TOTALA-EXE.md §98 rather than
 * from a play-test, and the definitions below are shipped ones with the flag
 * added, which is what a mod would do.
 */
namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 64, int height = 64)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /** `armsolar_dead`, from `features/Corpses/arm_corpses.tdf`. */
        FeatureDefinition makeSolarWreckDef()
        {
            FeatureDefinition d{};
            d.name = "armsolar_dead";
            d.footprintX = 5;
            d.footprintZ = 5;
            d.height = 40_ss;
            d.reclaimable = true;
            d.autoreclaimable = true;
            d.metal = 116;
            d.blocking = true;
            return d;
        }

        /** ARMSOLAR: BuildTime 2495, and what the corpse above came from. */
        UnitDefinition makeSolarDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = false;
            d.maxHitPoints = 326;
            d.buildTime = 2495u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5u, 5u, 255u, 255u, 0u, 0u};
            // A building is placed through the yardmap, so it needs one; the
            // real ARMSOLAR is five by five of closed ground.
            d.yardMap = Grid<YardMapCell>(5, 5, YardMapCell::Ground);
            return d;
        }

        /** ARMCK, the Arm construction kbot: WorkerTime 80, so 2 per tick. */
        UnitDefinition makeBuilderDef(bool canResurrect)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.builder = true;
            d.canReclamate = true;
            d.canResurrect = canResurrect;
            d.workerTimePerTick = 80u / 30u;
            d.maxHitPoints = 260;
            d.buildTime = 0u;
            d.sightDistance = 273u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        struct ResurrectFixture
        {
            std::shared_ptr<CobScript> script;
            GameSimulation sim;
            PlayerId owner;
            UnitId builder;

            explicit ResurrectFixture(bool canResurrect = true)
                : script(makeEmptyCobScript({"base"})),
                  sim(makeFlatTerrain(), 0u, 0, 0),
                  owner(addPlayer(sim, "us")),
                  builder(UnitId(0))
            {
                std::vector<UnitPieceDefinition> pieces{
                    UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
                sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

                sim.unitDefinitions["ARMSOLAR"] = makeSolarDef();
                sim.unitDefinitions["ARMCK"] = makeBuilderDef(canResurrect);
                sim.unitScriptDefinitions["ARMSOLAR"] = *script;
                sim.unitScriptDefinitions["ARMCK"] = *script;

                builder = addUnitOfType(sim, "ARMCK", owner, SimVector(0_ss, 0_ss, 0_ss), script);

                // An empty COB script never fires SetBuildStance, and the
                // handler waits for the arms as reclaim's does, so the
                // fixture asserts what the script would have.
                sim.getUnitState(builder).inBuildStance = true;
            }

            /** Runs until the order clears, or gives up. Returns ticks taken. */
            int runUntilOrderDone(int limit)
            {
                for (int t = 1; t <= limit; ++t)
                {
                    sim.tick();
                    if (sim.getUnitState(builder).orders.empty())
                    {
                        return t;
                    }
                }
                return -1;
            }
        };
    }

    TEST_CASE("a corpse is resurrected into the unit it came from", "[resurrect]")
    {
        ResurrectFixture f;
        auto wreckDef = f.sim.featureDefinitions.insert(makeSolarWreckDef());
        auto wreckId = f.sim.addFeature(wreckDef, 34, 34).value();
        auto wreckPosition = f.sim.getFeature(wreckId).position;

        // Stand the builder on top of it so nothing has to be walked.
        f.sim.getUnitState(f.builder).position = wreckPosition;
        f.sim.getUnitState(f.builder).orders.push_back(ResurrectOrder(wreckId));

        auto ticks = f.runUntilOrderDone(4000);
        REQUIRE(ticks > 0);

        SECTION("the corpse is gone and a solar collector stands in its place")
        {
            REQUIRE_FALSE(f.sim.tryGetFeature(wreckId).has_value());

            std::optional<UnitId> raised;
            for (const auto& [id, unit] : f.sim.units)
            {
                if (id != f.builder && unit.unitType == "ARMSOLAR")
                {
                    raised = id;
                }
            }
            REQUIRE(raised.has_value());

            const auto& unit = f.sim.getUnitState(*raised);
            REQUIRE(unit.owner == f.owner);
            REQUIRE(unit.position.x == wreckPosition.x);
            REQUIRE(unit.position.z == wreckPosition.z);

            // 0x405219/0x405226: complete rather than a nanoframe, and on
            // exactly one hit point.
            REQUIRE_FALSE(unit.isBeingBuilt(f.sim.unitDefinitions.at("ARMSOLAR")));
            REQUIRE(unit.hitPoints == 1);
        }

        SECTION("it takes BuildTime * 0.3 / (WorkerTime/30) ticks")
        {
            // ARMSOLAR 2495 * 0.3 = 748 (integer), ARMCK 80/30 = 2, so 374
            // ticks of work. This is the one job of the three that scales
            // with the builder: capture does not, and neither does reclaim.
            //
            // Two ticks either side of the work itself: one to take up the
            // job and get the arms out, as reclaim and build also spend, and
            // one on which the unit is finally placed.
            REQUIRE(ticks == 374 + 2);
        }
    }

    TEST_CASE("what cannot be resurrected", "[resurrect]")
    {
        SECTION("a unit without CanResurrect refuses the order outright")
        {
            ResurrectFixture f(false);
            auto wreckDef = f.sim.featureDefinitions.insert(makeSolarWreckDef());
        auto wreckId = f.sim.addFeature(wreckDef, 34, 34).value();
            f.sim.getUnitState(f.builder).position = f.sim.getFeature(wreckId).position;
            f.sim.getUnitState(f.builder).orders.push_back(ResurrectOrder(wreckId));

            // Dropped on the first tick, and the corpse is untouched.
            REQUIRE(f.runUntilOrderDone(5) == 1);
            REQUIRE(f.sim.tryGetFeature(wreckId).has_value());
        }

        SECTION("a feature that is not reclaimable")
        {
            // 0x404E0F tests the reclaimable bit, not a resurrect bit of its
            // own: what you could not have reclaimed you cannot raise.
            ResurrectFixture f;
            auto def = makeSolarWreckDef();
            def.reclaimable = false;
            auto wreckId = f.sim.addFeature(f.sim.featureDefinitions.insert(def), 34, 34).value();
            f.sim.getUnitState(f.builder).position = f.sim.getFeature(wreckId).position;
            f.sim.getUnitState(f.builder).orders.push_back(ResurrectOrder(wreckId));

            REQUIRE(f.runUntilOrderDone(5) == 1);
            REQUIRE(f.sim.tryGetFeature(wreckId).has_value());
        }

        SECTION("a feature whose name has no underscore")
        {
            // The mapping is a string operation, not a table (0x404F36), so
            // a tree is simply a name that does not split.
            ResurrectFixture f;
            auto def = makeSolarWreckDef();
            def.name = "AlgaeMoss";
            auto treeId = f.sim.addFeature(f.sim.featureDefinitions.insert(def), 34, 34).value();
            f.sim.getUnitState(f.builder).position = f.sim.getFeature(treeId).position;
            f.sim.getUnitState(f.builder).orders.push_back(ResurrectOrder(treeId));

            REQUIRE(f.runUntilOrderDone(5) == 1);
            REQUIRE(f.sim.tryGetFeature(treeId).has_value());
        }

        SECTION("a corpse whose prefix is not a unit type")
        {
            ResurrectFixture f;
            auto def = makeSolarWreckDef();
            def.name = "nosuchunit_dead";
            auto wreckId = f.sim.addFeature(f.sim.featureDefinitions.insert(def), 34, 34).value();
            f.sim.getUnitState(f.builder).position = f.sim.getFeature(wreckId).position;
            f.sim.getUnitState(f.builder).orders.push_back(ResurrectOrder(wreckId));

            REQUIRE(f.runUntilOrderDone(5) == 1);
            REQUIRE(f.sim.tryGetFeature(wreckId).has_value());
        }
    }

    TEST_CASE("the second-stage wreck resolves to the same unit", "[resurrect]")
    {
        // `armsolar_heap` truncates to `armsolar` just as `armsolar_dead`
        // does. Presumably not deliberate, but it is what the code says, and
        // a heap is the thing most likely to be standing about when someone
        // finally builds a resurrector.
        ResurrectFixture f;
        auto def = makeSolarWreckDef();
        def.name = "armsolar_heap";
        def.metal = 58;
        auto heapId = f.sim.addFeature(f.sim.featureDefinitions.insert(def), 34, 34).value();
        f.sim.getUnitState(f.builder).position = f.sim.getFeature(heapId).position;
        f.sim.getUnitState(f.builder).orders.push_back(ResurrectOrder(heapId));

        REQUIRE(f.runUntilOrderDone(4000) > 0);
        REQUIRE_FALSE(f.sim.tryGetFeature(heapId).has_value());

        auto raised = false;
        for (const auto& [id, unit] : f.sim.units)
        {
            if (id != f.builder && unit.unitType == "ARMSOLAR")
            {
                raised = true;
            }
        }
        REQUIRE(raised);
    }

    TEST_CASE("the countdown belongs to the order", "[resurrect]")
    {
        // As capture's progress does (§96): the original keeps it in the
        // mission record, so a builder called away loses the work and starting
        // again starts from the beginning.
        ResurrectFixture f;
        auto wreckDef = f.sim.featureDefinitions.insert(makeSolarWreckDef());
        auto wreckId = f.sim.addFeature(wreckDef, 34, 34).value();
        f.sim.getUnitState(f.builder).position = f.sim.getFeature(wreckId).position;
        f.sim.getUnitState(f.builder).orders.push_back(ResurrectOrder(wreckId));

        for (int i = 0; i < 100; ++i)
        {
            f.sim.tick();
        }

        {
            const auto& orders = f.sim.getUnitState(f.builder).orders;
            REQUIRE_FALSE(orders.empty());
            const auto& order = std::get<ResurrectOrder>(orders.front());
            REQUIRE(order.remainingTicks.has_value());
            REQUIRE(*order.remainingTicks < 374);
        }

        // Called away, then told to do it again: the count is fresh.
        f.sim.getUnitState(f.builder).orders.clear();
        f.sim.getUnitState(f.builder).orders.push_back(ResurrectOrder(wreckId));
        f.sim.tick();

        const auto& orders = f.sim.getUnitState(f.builder).orders;
        REQUIRE_FALSE(orders.empty());
        const auto& order = std::get<ResurrectOrder>(orders.front());
        REQUIRE(order.remainingTicks.has_value());
        REQUIRE(*order.remainingTicks == 373);
    }
}
