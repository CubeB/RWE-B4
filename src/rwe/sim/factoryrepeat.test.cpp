#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/game/PlayerCommandApplication.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/game/save_util.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <optional>
#include <rwe/sim/sim_test_util.h>
#include <string>

/**
 * The opt-in factory repeat (#417, ROADMAP Phase 7): a toggle on a factory
 * that puts each finished item back on the end of its own build queue instead
 * of consuming it.
 *
 * Four properties are pinned here, and they are four different kinds of claim.
 *
 * Off changes nothing. The default is not "the feature is subtle", it is the
 * only reason an existing game is unaffected: a factory whose flag nobody set
 * must drain its queue exactly as TA does, and this is the test that says so
 * rather than assuming it.
 *
 * On re-queues. Not "keeps building" -- the entry goes to the *back*, so a
 * queue of two types alternates instead of restarting. That is the whole
 * difference between a loop and a starvation, and a test that only counts
 * output units would pass either.
 *
 * The flag round-trips through save and load, in saveload.test.cpp, because
 * that is where the round trip lives.
 *
 * Two units differing only in the flag hash differently, in
 * GameHash_util.test.cpp, beside the field table's own pin.
 */
namespace rwe
{
    namespace
    {
        void registerModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /**
         * A plant: a fixed builder with a worker time and a yard map, and
         * storage to match. The storage is load-bearing -- the simulation
         * recomputes a player's capacity every second from the units that
         * exist, and a yard with no storage is clamped to nothing on the first
         * second boundary and stalls there.
         */
        UnitDefinition makeYardDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = false;
            d.canMove = false;
            d.builder = true;
            d.workerTimePerTick = 10;
            d.metalStorage = Metal(10000.0f);
            d.energyStorage = Energy(10000.0f);
            d.maxHitPoints = 1000;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{6u, 6u, 255u, 255u, 0u, 0u};
            d.yardMap = Grid<YardMapCell>(6, 6, YardMapCell::Ground);
            return d;
        }

        /** What a yard turns out. Cheap, so nothing here ever stalls on metal. */
        UnitDefinition makeProductDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxVelocity = 6_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 100u;
            d.buildCostMetal = Metal(1.0f);
            d.buildCostEnergy = Energy(1.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /**
         * A yard with a queue, and a count of what it has finished.
         *
         * The two products differ only in name, so a test that wants to tell
         * which one the factory is working on can, and neither is cheap enough
         * to finish before the other.
         */
        struct Yard
        {
            std::shared_ptr<CobScript> script;
            GameSimulation sim;
            PlayerId player;
            UnitId yardId;

            Yard()
                : script(makeEmptyCobScript({"base"})),
                  sim(makeFlatTerrain(128, 128), 0u, 0, 0),
                  player(addWellStockedPlayer(sim, "CORE")),
                  yardId(UnitId(0))
            {
                sim.unitDefinitions["YARD"] = makeYardDef();
                sim.unitDefinitions["TANK"] = makeProductDef();
                sim.unitDefinitions["BOT"] = makeProductDef();
                sim.unitScriptDefinitions["TANK"] = *script;
                sim.unitScriptDefinitions["BOT"] = *script;
                registerModel(sim);

                yardId = addUnitOfType(sim, "YARD", player, SimVector(600_ss, 0_ss, 600_ss), script);
                // The stance a StartBuilding script would set; there is no script here.
                sim.getUnitState(yardId).inBuildStance = true;
            }

            void queue(const std::string& unitType, int count)
            {
                sim.getUnitState(yardId).buildQueue.emplace_back(unitType, count);
            }

            /**
             * How many of this type are *finished* -- raised and no longer
             * being worked on.
             *
             * Not the same as existing: a unit appears on the pad the moment
             * the yard asks for it, and its queue entry is not consumed until
             * the frame is complete. Counting appearances therefore reads the
             * queue one entry early, which is exactly the state these tests
             * are about.
             */
            int completed(const std::string& unitType)
            {
                int n = 0;
                for (const auto& [id, unit] : sim.units)
                {
                    if (unit.unitType == unitType && !unit.isDead()
                        && !unit.isBeingBuilt(sim.unitDefinitions.at(unitType)))
                    {
                        ++n;
                    }
                }
                return n;
            }

            /** The queue as a list of (type, count), for saying what is where. */
            std::vector<std::pair<std::string, int>> queueContents()
            {
                return {sim.getUnitState(yardId).buildQueue.begin(), sim.getUnitState(yardId).buildQueue.end()};
            }
        };

        /**
         * Ticks until the yard has finished `wanted` of `unitType`, or gives
         * up, then lets it act on that. Finishing, not appearing -- see
         * Yard::completed.
         *
         * The two ticks at the end matter: the frame reads complete on one
         * tick and the yard's behaviour pass works its queue off on the next,
         * so stopping the moment the unit looked finished would read every
         * queue one entry behind. Two is not a guess at a latency -- the yard
         * makes no further change to its queue until another item *finishes*,
         * which cannot happen that fast.
         *
         * Bounded rather than open-ended on purpose: a queue that is supposed
         * to loop for ever would otherwise hang the suite instead of failing.
         * The budget is generous because a yard's pad has to clear between
         * products -- the finished unit is told to move off its footprint
         * before the next one goes down -- and the point of these tests is the
         * queue, not how fast a tank walks.
         */
        bool tickUntilProduced(Yard& yard, const std::string& unitType, int wanted, int limit = 4000)
        {
            for (int i = 0; i < limit; ++i)
            {
                yard.sim.tick();
                if (yard.completed(unitType) >= wanted)
                {
                    tick(yard.sim, 2);
                    return true;
                }
            }
            return false;
        }
    }

    TEST_CASE("a factory with the repeat toggle off drains its queue", "[factoryrepeat]")
    {
        // The control, and the reason the feature is safe to add at all: TA's
        // factory consumes its queue, and a unit nobody has toggled must keep
        // doing exactly that.
        auto yard = Yard();
        yard.queue("TANK", 1);

        REQUIRE(yard.queueContents() == std::vector<std::pair<std::string, int>>{{"TANK", 1}});
        REQUIRE_FALSE(yard.sim.getUnitState(yard.yardId).repeatBuildQueue);

        REQUIRE(tickUntilProduced(yard, "TANK", 1));

        // One built, and the queue empty -- consumed, not re-queued. Left to
        // run on it would clear the queue and then stop, which is what
        // "drains" means.
        REQUIRE(yard.queueContents().empty());
        tick(yard.sim, 500);
        REQUIRE(yard.completed("TANK") == 1);
    }

    TEST_CASE("the repeat toggle re-queues a finished item to the back of the queue", "[factoryrepeat]")
    {
        auto yard = Yard();
        yard.sim.getUnitState(yard.yardId).repeatBuildQueue = true;
        yard.queue("TANK", 1);

        REQUIRE(tickUntilProduced(yard, "TANK", 1));

        // The entry is still there, with its count back at one. This is the
        // whole feature: the item that was just finished went to the back
        // rather than being consumed, and one entry on a queue of one is also
        // the back.
        REQUIRE(yard.queueContents() == std::vector<std::pair<std::string, int>>{{"TANK", 1}});

        // And it keeps going: three of them, from a queue that started with
        // one entry. This is the part that would fail if the flag only
        // suppressed the pop for a tick.
        REQUIRE(tickUntilProduced(yard, "TANK", 3));
    }

    TEST_CASE("the repeat toggle keeps a queue of two in order rather than restarting it", "[factoryrepeat]")
    {
        // The distinction that separates a loop from a starvation. Both a
        // correct implementation and a "restart the queue" one produce both
        // unit types for ever, so counting output would pass either; what
        // tells them apart is the order the queue is worked in, and this
        // watches that.
        auto yard = Yard();
        yard.sim.getUnitState(yard.yardId).repeatBuildQueue = true;
        yard.queue("TANK", 1);
        yard.queue("BOT", 1);

        // One TANK, and the queue is BOT first with the finished TANK behind
        // it. That ordering is the whole claim: the entry rejoined at the
        // *back*, so BOT is next. An implementation that pushed the finished
        // entry at the front, or restarted the queue, would put TANK first
        // here and starve BOT for ever -- and a test that only counted output
        // would have passed either.
        REQUIRE(tickUntilProduced(yard, "TANK", 1));
        REQUIRE(yard.queueContents() == std::vector<std::pair<std::string, int>>{{"BOT", 1}, {"TANK", 1}});

        // Then BOT, and the queue is back to TANK first with BOT behind it.
        REQUIRE(tickUntilProduced(yard, "BOT", 1));
        REQUIRE(yard.queueContents() == std::vector<std::pair<std::string, int>>{{"TANK", 1}, {"BOT", 1}});

        // And round again, which a "restart the queue" implementation would
        // also reach -- but by then the second entry has been built, and one
        // that never was would have starved it.
        REQUIRE(tickUntilProduced(yard, "TANK", 2));
        REQUIRE(yard.completed("BOT") == 1);
    }

    TEST_CASE("turning the repeat toggle off restores the drain", "[factoryrepeat]")
    {
        // Off is restored exactly, not approximately: a player who turns it
        // off mid-queue gets the original behaviour back with what is already
        // queued intact, and the flag never touches the queue itself.
        auto yard = Yard();
        yard.sim.getUnitState(yard.yardId).repeatBuildQueue = true;
        yard.queue("TANK", 1);
        yard.queue("BOT", 1);

        REQUIRE(tickUntilProduced(yard, "TANK", 1));
        REQUIRE(yard.queueContents() == std::vector<std::pair<std::string, int>>{{"BOT", 1}, {"TANK", 1}});

        yard.sim.getUnitState(yard.yardId).repeatBuildQueue = false;
        REQUIRE(yard.queueContents() == std::vector<std::pair<std::string, int>>{{"BOT", 1}, {"TANK", 1}});

        REQUIRE(tickUntilProduced(yard, "BOT", 1));
        REQUIRE(tickUntilProduced(yard, "TANK", 2));
        REQUIRE(yard.queueContents().empty());

        tick(yard.sim, 500);
        REQUIRE(yard.completed("TANK") == 2);
        REQUIRE(yard.completed("BOT") == 1);
    }

    TEST_CASE("the repeat toggle is per unit, not per game", "[factoryrepeat]")
    {
        // Two yards, one on and one off, sharing a player. If the flag were
        // anywhere but on the unit -- a game option, a player field -- the two
        // could not disagree, and this would be testing nothing.
        //
        // Each yard is given its own product type so the assertion is about
        // what each one did rather than about a shared total, which would be
        // satisfiable by either yard doing all the work.
        auto yard = Yard();
        auto otherScript = makeEmptyCobScript({"base"});
        auto otherId = addUnitOfType(yard.sim, "YARD", yard.player, SimVector(900_ss, 0_ss, 600_ss), otherScript);
        yard.sim.getUnitState(otherId).inBuildStance = true;

        yard.sim.getUnitState(yard.yardId).repeatBuildQueue = true;
        yard.queue("TANK", 1);
        yard.sim.getUnitState(otherId).buildQueue.emplace_back("BOT", 1);

        // The looping one keeps its entry; the other has consumed its.
        REQUIRE(tickUntilProduced(yard, "TANK", 1));
        REQUIRE(tickUntilProduced(yard, "BOT", 1));

        REQUIRE(yard.queueContents() == std::vector<std::pair<std::string, int>>{{"TANK", 1}});
        REQUIRE(yard.sim.getUnitState(otherId).buildQueue.empty());

        // And the drain is not merely slower: give it a long run and it stays
        // drained, while the looping one is well past its second tank.
        tick(yard.sim, 2000);
        REQUIRE(yard.completed("BOT") == 1);
        REQUIRE(yard.sim.getUnitState(otherId).buildQueue.empty());
        REQUIRE(yard.completed("TANK") >= 2);
    }

    TEST_CASE("the repeat toggle reaches the simulation as a command, not from the panel", "[factoryrepeat]")
    {
        // Trap two of CLAUDE.md's determinism rules, in the shape this feature
        // could have got wrong: a panel that wrote the flag straight onto the
        // unit would put this machine's answer into the sync hash a round trip
        // before any peer had heard of it. So the value has to arrive the way
        // every other order does, and this goes through the same applier the
        // network path and the arena use.
        auto yard = Yard();
        yard.queue("TANK", 1);
        REQUIRE_FALSE(yard.sim.getUnitState(yard.yardId).repeatBuildQueue);

        REQUIRE(applyUnitCommandToSimulation(
            yard.sim, yard.player, PlayerUnitCommand(yard.yardId, PlayerUnitCommand::SetBuildQueueRepeat{true})));
        REQUIRE(yard.sim.getUnitState(yard.yardId).repeatBuildQueue);

        // Off again is a command like any other, and restores exactly.
        REQUIRE(applyUnitCommandToSimulation(
            yard.sim, yard.player, PlayerUnitCommand(yard.yardId, PlayerUnitCommand::SetBuildQueueRepeat{false})));
        REQUIRE_FALSE(yard.sim.getUnitState(yard.yardId).repeatBuildQueue);

        // And the refusal rules still apply to it: a player who does not own
        // the factory cannot set the flag on it, which is what stops a peer
        // reaching across and reconfiguring somebody else's production.
        REQUIRE_FALSE(applyUnitCommandToSimulation(
            yard.sim,
            PlayerId(yard.player.value + 1),
            PlayerUnitCommand(yard.yardId, PlayerUnitCommand::SetBuildQueueRepeat{true})));
        REQUIRE_FALSE(yard.sim.getUnitState(yard.yardId).repeatBuildQueue);
    }
}
