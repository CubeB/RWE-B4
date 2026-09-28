// EconomyManager::refresh: the buckets it fills, and the per-type unit
// definition index it fills them from.
//
// The index is the interesting half. It answers "what is this unit" once per
// unit TYPE per pass rather than once per unit per pass, which is the whole
// point of it and also the whole risk of it: a bucket that quietly draws its
// answer from the wrong row is a different AI, not a faster one. So the tests
// below check it two ways -- that every flag matches the definition it was
// worked out from, and that the buckets still land where they used to.

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiSideUnits.h>
#include <rwe/ai/AiUnitDefIndex.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/EconomyManager.h>
#include <rwe/ai/ai_test_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        using Catch::Approx;

        /**
         * A simulation, one AI player, and the shared test world.
         *
         * Named for what it is rather than to match the shared `addPlayer`:
         * an anonymous namespace inside namespace rwe joins the shared
         * overload set rather than shadowing it, so a local `addPlayer` here
         * would make the call ambiguous instead of overriding it.
         */
        struct EconomyWorld
        {
            GameSimulation sim{makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0};
            PlayerId ai{addPlayer(sim, "ai", GamePlayerType::Computer, "ARM")};
            std::shared_ptr<CobScript> script = makeEmptyCobScript();
            // Resolved in the body, not the initialiser list: defineWorld is
            // what puts the definitions there to resolve out of.
            AiSideUnits sideUnits;

            EconomyWorld()
            {
                defineWorld(sim);
                sideUnits = resolveAiSideUnits(sim, "ARM");
                // The pieces ai_test_util leaves out that the buckets ask
                // about: a metal maker that can be switched off, and a hull
                // under each of the side's own names.
                auto maker = makeDef(false, false, false, "", 50u);
                maker.onOffable = true;
                maker.makesMetal = Metal(2.0f);
                sim.unitDefinitions["ARMMAKR"] = maker;
            }

            UnitId give(const std::string& type, const SimVector& at)
            {
                return addUnit(sim, type, ai, at, script);
            }
        };

        /**
         * What the definition says, read the slow way.
         *
         * The oracle for the index: every flag it publishes has to be the
         * value this produces, or the index is answering from somewhere else.
         */
        AiUnitDefFacts factsReadDirectly(const GameSimulation& sim, const AiSideUnits& sideUnits, const std::string& unitType)
        {
            const auto& def = sim.unitDefinitions.at(unitType);
            AiUnitDefFacts facts;
            facts.definition = &def;
            facts.buildTime = def.buildTime;
            facts.isMobile = def.isMobile;
            facts.builder = def.builder;
            facts.onOffable = def.onOffable;
            facts.makesMetal = def.makesMetal.value > 0.0f;
            facts.commander = def.commander;
            facts.transport = def.isTransport();
            facts.canFly = def.canFly;
            facts.canAttack = def.canAttack;
            facts.armed = !def.weapon1.empty() || !def.weapon2.empty();
            facts.navalCombatUnit = (!sideUnits.destroyer.empty() && unitType == sideUnits.destroyer)
                || (!sideUnits.submarine.empty() && unitType == sideUnits.submarine)
                || (!sideUnits.cruiser.empty() && unitType == sideUnits.cruiser)
                || (!sideUnits.battleship.empty() && unitType == sideUnits.battleship)
                || (!sideUnits.antiAirShip.empty() && unitType == sideUnits.antiAirShip)
                || (!sideUnits.scoutShip.empty() && unitType == sideUnits.scoutShip);
            facts.scoutType = isAiScoutType(sideUnits, unitType);
            facts.antiAirType = isAiAntiAirType(sideUnits, unitType);
            return facts;
        }

        void requireSameFacts(const AiUnitDefFacts& expected, const AiUnitDefFacts& actual, const std::string& unitType)
        {
            INFO("unit type " << unitType);
            REQUIRE(actual.definition == expected.definition);
            REQUIRE(actual.buildTime == expected.buildTime);
            REQUIRE(actual.isMobile == expected.isMobile);
            REQUIRE(actual.builder == expected.builder);
            REQUIRE(actual.onOffable == expected.onOffable);
            REQUIRE(actual.makesMetal == expected.makesMetal);
            REQUIRE(actual.commander == expected.commander);
            REQUIRE(actual.transport == expected.transport);
            REQUIRE(actual.canFly == expected.canFly);
            REQUIRE(actual.canAttack == expected.canAttack);
            REQUIRE(actual.armed == expected.armed);
            REQUIRE(actual.navalCombatUnit == expected.navalCombatUnit);
            REQUIRE(actual.scoutType == expected.scoutType);
            REQUIRE(actual.antiAirType == expected.antiAirType);
        }
    }

    TEST_CASE("AiUnitDefIndex answers for every type the test world defines")
    {
        EconomyWorld world;

        AiUnitDefIndex index;
        index.beginPass();
        for (const auto& [unitType, definition] : world.sim.unitDefinitions)
        {
            // Asked twice on purpose: the second ask is the one that has to
            // come out of the table rather than the game's map.
            requireSameFacts(factsReadDirectly(world.sim, world.sideUnits, unitType), index.resolve(unitType, world.sim, world.sideUnits), unitType);
            requireSameFacts(factsReadDirectly(world.sim, world.sideUnits, unitType), index.resolve(unitType, world.sim, world.sideUnits), unitType);
        }
        REQUIRE(index.resolvedCount() == world.sim.unitDefinitions.size());
    }

    TEST_CASE("AiUnitDefIndex gives the same answer whatever order the types arrive in")
    {
        EconomyWorld world;
        std::vector<std::string> types;
        for (const auto& [unitType, definition] : world.sim.unitDefinitions)
        {
            types.push_back(unitType);
        }
        REQUIRE(types.size() > 4);

        // Forward, so the first ask of each type misses and the rows are
        // filled in the order they were written.
        AiUnitDefIndex forward;
        forward.beginPass();
        for (const auto& type : types)
        {
            forward.resolve(type, world.sim, world.sideUnits);
        }

        // Backward, so every row lands in a slot another type is already in
        // and the probe has to walk past it. A rehash that lost or duplicated
        // a row would show up here and nowhere else.
        std::reverse(types.begin(), types.end());
        AiUnitDefIndex backward;
        backward.beginPass();
        for (const auto& type : types)
        {
            requireSameFacts(factsReadDirectly(world.sim, world.sideUnits, type), backward.resolve(type, world.sim, world.sideUnits), type);
        }
        REQUIRE(backward.resolvedCount() == forward.resolvedCount());
    }

    TEST_CASE("AiUnitDefIndex grows past its opening size and still finds every type")
    {
        EconomyWorld world;
        // Far more types than the table starts with, so it rehashes twice
        // while it fills and every row after the first move lands in a table
        // that no longer exists.
        std::vector<std::string> types;
        for (const auto& [unitType, definition] : world.sim.unitDefinitions)
        {
            types.push_back(unitType);
        }
        for (int i = 0; i < 200; ++i)
        {
            auto& def = world.sim.unitDefinitions["EXTRA" + std::to_string(i)];
            def = world.sim.unitDefinitions.at("ARMPW");
            types.push_back("EXTRA" + std::to_string(i));
        }

        AiUnitDefIndex index;
        index.beginPass();
        for (const auto& type : types)
        {
            index.resolve(type, world.sim, world.sideUnits);
        }
        REQUIRE(index.resolvedCount() == types.size());

        // And every one of them is still findable afterwards.
        index.beginPass();
        for (const auto& type : types)
        {
            requireSameFacts(factsReadDirectly(world.sim, world.sideUnits, type), index.resolve(type, world.sim, world.sideUnits), type);
        }
    }

    TEST_CASE("AiUnitDefIndex is emptied by beginPass, so an edited definition is not answered from the last pass")
    {
        EconomyWorld world;
        AiUnitDefIndex index;
        index.beginPass();
        REQUIRE(index.resolve("ARMPW", world.sim, world.sideUnits).armed);
        REQUIRE(index.resolvedCount() == 1);

        // The way a test hands the AI a different tank: the same type, a
        // different definition, with nothing else changed.
        world.sim.unitDefinitions.at("ARMPW").weapon1 = "";

        // Still armed within the pass it was resolved in -- the index is a
        // snapshot of one pass, not a live view of the map.
        REQUIRE(index.resolve("ARMPW", world.sim, world.sideUnits).armed);

        index.beginPass();
        REQUIRE_FALSE(index.resolve("ARMPW", world.sim, world.sideUnits).armed);
    }

    TEST_CASE("AiUnitDefIndex refuses a type the game data does not define")
    {
        EconomyWorld world;
        AiUnitDefIndex index;
        index.beginPass();
        REQUIRE_THROWS_AS(index.resolve("NOTAUNIT", world.sim, world.sideUnits), std::out_of_range);
    }

    TEST_CASE("refresh sorts our own units into the same buckets it always did")
    {
        EconomyWorld world;
        auto commander = world.give("ARMCOM", SimVector(0_ss, 0_ss, 0_ss));
        auto lab = world.give("ARMLAB", SimVector(0_ss, 0_ss, -80_ss));
        auto kbot = world.give("ARMCK", SimVector(-60_ss, 0_ss, 60_ss));
        auto solar = world.give("ARMSOLAR", SimVector(100_ss, 0_ss, 0_ss));
        auto maker = world.give("ARMMAKR", SimVector(140_ss, 0_ss, 0_ss));
        auto tank = world.give("ARMPW", SimVector(200_ss, 0_ss, 0_ss));
        auto tower = world.give("ARMLLT", SimVector(-200_ss, 0_ss, 0_ss));
        auto jet = world.give("ARMJETH", SimVector(-240_ss, 0_ss, 0_ss));
        auto peeper = world.give("ARMPEEP", SimVector(300_ss, 0_ss, 0_ss));
        auto atlas = world.give("ARMATLAS", SimVector(340_ss, 0_ss, 0_ss));
        auto destroyer = world.give("ARMROY", SimVector(400_ss, 0_ss, 0_ss));
        auto scoutShip = world.give("ARMPT", SimVector(440_ss, 0_ss, 0_ss));

        EconomyManager economy;
        AiBlackboard bb;
        AiTuningProfile profile;
        economy.refresh(world.sim, world.ai, profile, bb);

        // A builder that is not a factory goes to no bucket but the counts.
        REQUIRE(bb.factories == std::vector<UnitId>{lab});
        REQUIRE(bb.combatUnits == std::vector<UnitId>{tank});
        REQUIRE(bb.antiAirUnits == std::vector<UnitId>{jet});
        REQUIRE(bb.scoutUnits == std::vector<UnitId>{peeper});
        REQUIRE(bb.transports == std::vector<UnitId>{atlas});
        REQUIRE(bb.navalCombatUnits == std::vector<UnitId>{destroyer, scoutShip});
        // The commander is a builder and mobile, so it is not a factory, and it
        // is not idle either: it has a home position and an anchor.
        REQUIRE(bb.commanderUnitId == commander);
        REQUIRE((bb.baseAnchor == SimVector(0_ss, 0_ss, 0_ss)));
        // Switchable metal makers, and nothing else: a solar makes energy.
        REQUIRE(bb.metalMakers == std::vector<UnitId>{maker});
        REQUIRE(bb.ownedCompletedCounts.at("ARMSOLAR") == 1);
        REQUIRE(bb.standingBuildings.count(solar.value) == 1);
        REQUIRE(bb.idleBuilders == std::vector<UnitId>{commander, kbot});
        REQUIRE(bb.armySize == 1);

        // Counted by type, frames and finished alike.
        REQUIRE(bb.ownedTotalCounts.at("ARMPW") == 1);
        REQUIRE(bb.ownedCompletedCounts.at("ARMPW") == 1);
        REQUIRE(bb.ownedCompletedCounts.at("ARMLAB") == 1);
        REQUIRE(bb.standingBuildings.count(lab.value) == 1);
        REQUIRE(bb.standingBuildings.count(tower.value) == 1);
        // The commander's own frame is standing as a unit, not a building: it
        // is mobile, so it goes in standingUnits whichever way it is finished.
        REQUIRE(bb.standingUnits.count(commander.value) == 1);
        REQUIRE(bb.standingBuildings.count(commander.value) == 0);
        REQUIRE(bb.standingUnits.size() == 8);
    }

    TEST_CASE("refresh counts a half-built frame and finds it orphaned when nobody is on it")
    {
        EconomyWorld world;
        world.give("ARMCOM", SimVector(0_ss, 0_ss, 0_ss));
        auto labFrame = world.give("ARMLAB", SimVector(0_ss, 0_ss, -80_ss));
        world.sim.getUnitState(labFrame).buildTimeCompleted = 0;

        EconomyManager economy;
        AiBlackboard bb;
        AiTuningProfile profile;
        economy.refresh(world.sim, world.ai, profile, bb);

        REQUIRE(bb.ownedTotalCounts.at("ARMLAB") == 1);
        REQUIRE(bb.ownedCompletedCounts.count("ARMLAB") == 0);
        // A frame is standing as a unit, and nobody is building it.
        REQUIRE(bb.standingUnits.at(labFrame.value).underConstruction);
        REQUIRE(bb.standingBuildings.empty());
        REQUIRE(bb.orphanedFrames == std::vector<UnitId>{labFrame});
    }

    TEST_CASE("refresh does not count a unit a mission still holds")
    {
        EconomyWorld world;
        world.give("ARMCOM", SimVector(0_ss, 0_ss, 0_ss));
        auto tank = world.give("ARMPW", SimVector(200_ss, 0_ss, 0_ss));
        world.sim.getUnitState(tank).heldByMission = true;

        EconomyManager economy;
        AiBlackboard bb;
        AiTuningProfile profile;
        economy.refresh(world.sim, world.ai, profile, bb);

        // Counted, but not planned for and not sent anywhere.
        REQUIRE(bb.ownedTotalCounts.at("ARMPW") == 1);
        REQUIRE(bb.ownedCompletedCounts.count("ARMPW") == 0);
        REQUIRE(bb.combatUnits.empty());
        // The mission's tank is not even standing as one of ours; the
        // commander's, which is not held by anything, is the only thing that is.
        REQUIRE(bb.standingUnits.count(tank.value) == 0);
        REQUIRE(bb.standingUnits.size() == 1);
    }

    TEST_CASE("refresh sees a definition edited since the last pass, and the same answer twice within one")
    {
        EconomyWorld world;
        world.give("ARMCOM", SimVector(0_ss, 0_ss, 0_ss));
        auto tank = world.give("ARMPW", SimVector(200_ss, 0_ss, 0_ss));

        EconomyManager economy;
        AiBlackboard bb;
        AiTuningProfile profile;
        economy.refresh(world.sim, world.ai, profile, bb);
        REQUIRE(bb.combatUnits == std::vector<UnitId>{tank});

        // Take the gun away. The pass before this one resolved ARMPW as an
        // armed tank, and an index that outlived the pass would still say so.
        world.sim.unitDefinitions.at("ARMPW").weapon1 = "";
        economy.refresh(world.sim, world.ai, profile, bb);
        REQUIRE(bb.combatUnits.empty());
    }

    TEST_CASE("refresh reads a builder's draw off the frame's own definition, not the builder's")
    {
        EconomyWorld world;
        auto commander = world.give("ARMCOM", SimVector(0_ss, 0_ss, 0_ss));
        auto kbot = world.give("ARMCK", SimVector(0_ss, 0_ss, 0_ss));

        // A big expensive frame, put up by hand rather than by the planner:
        // the draw is what the builder is committed to, and it is worked out
        // from the frame's build cost and time, not the kbot's.
        auto def = makeDef(false, false, false, "", 50u);
        def.buildCostMetal = Metal(1000.0f);
        def.buildCostEnergy = Energy(1000.0f);
        def.buildTime = 6000u;
        world.sim.unitDefinitions["ARMHUGE"] = def;
        auto frame = world.give("ARMHUGE", SimVector(100_ss, 0_ss, 0_ss));
        world.sim.getUnitState(frame).buildTimeCompleted = 0;
        world.sim.getUnitState(kbot).behaviourState = UnitBehaviorStateBuilding{UnitId(frame.value), std::nullopt};
        world.sim.getUnitState(kbot).buildOrderUnitId = frame;
        world.sim.getUnitState(kbot).addOrder(BuildOrder("ARMHUGE", SimVector(100_ss, 0_ss, 0_ss)));

        EconomyManager economy;
        AiBlackboard bb;
        AiTuningProfile profile;
        economy.refresh(world.sim, world.ai, profile, bb);

        // 1000 metal over 6000 ticks at one worker-tick a tick, which is
        // 5 metal a second. Read off the FRAME's cost and time: worked out
        // from the kbot's 100-metal definition instead, it would be 0.5.
        REQUIRE(bb.metalCommitted.value == Catch::Approx(5.0f));
        REQUIRE(bb.orphanedFrames.empty());
        // The kbot is on a job, so the only idle builder left is the commander.
        REQUIRE(bb.idleBuilders == std::vector<UnitId>{commander});
        REQUIRE(bb.idleBuilderCount == 1);
    }
}
