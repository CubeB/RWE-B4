#include "MissionScripts.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/SimRandom.h>
#include <rwe/sim/UnitOrder.h>

namespace rwe
{
    namespace
    {
        /** The ordinary order a step becomes, for the steps that are one. */
        UnitOrder orderFor(const MissionStep& step)
        {
            using K = MissionStep::Kind;
            switch (step.kind)
            {
                case K::Move:
                    return MoveOrder(step.position);
                case K::AttackPoint:
                    return AttackOrder(step.position);
                case K::Guard:
                    return GuardOrder(*step.target);
                case K::Unload:
                    return UnloadOrder(step.position);
                default:
                    return BuildOrder(step.unitType, step.position);
            }
        }

        bool isOrderStep(MissionStep::Kind kind)
        {
            using K = MissionStep::Kind;
            return kind == K::Move || kind == K::AttackPoint || kind == K::Guard || kind == K::Unload || kind == K::Build;
        }

        /**
         * The steps a computer's unit goes on running while it rides in a
         * transport. The attach installs BeCarried and flushes the list only
         * for a human's unit (0x48ACF3), so a computer's cargo counts its
         * waits aboard -- AC13's Reapers wait 3545 seconds against their
         * Valkyries' 3500 plus the flight -- while anything that needs the
         * unit on the ground waits until it is set down.
         */
        bool runsAboard(MissionStep::Kind kind)
        {
            using K = MissionStep::Kind;
            return kind == K::Wait || kind == K::WaitForAttack || kind == K::MakeSelectable;
        }

        /**
         * Whether an enemy the unit's owner can see stands within `radius`:
         * the gather 0x40AD80 WAIT asks, which takes its candidates from the
         * same list weapon auto-acquire uses (0x40AA40) -- seen, alive, not
         * allied, not Immune -- and measures each squared axis in whole world
         * units, edge included.
         */
        bool enemyWithin(const GameSimulation& sim, const UnitState& unit, int radius)
        {
            auto limit = static_cast<float>(radius) * static_cast<float>(radius);
            for (const auto& [otherId, other] : sim.units)
            {
                if (other.isDead() || other.owner == unit.owner || other.immune || sim.arePlayersAllied(unit.owner, other.owner))
                {
                    continue;
                }
                if (!sim.canSeeUnit(unit.owner, otherId))
                {
                    continue;
                }
                auto dx = simScalarToFloat(other.position.x - unit.position.x);
                auto dz = simScalarToFloat(other.position.z - unit.position.z);
                if (std::floor(dx * dx) + std::floor(dz * dz) <= limit)
                {
                    return true;
                }
            }
            return false;
        }

        /**
         * ATTACKUTYPE's pick (0x401E2B-0x401F18): every live enemy unit of the
         * type anywhere on the map -- no sight, radar or Immunity test -- scored
         * d^2 - rand(d^2 / 2) in whole world units, lowest first, a tie going to
         * the later slot.
         */
        std::optional<UnitId> huntTarget(GameSimulation& sim, const UnitState& hunter, const std::string& unitType)
        {
            std::optional<UnitId> best;
            std::int64_t bestScore = 0;
            for (const auto& [otherId, other] : sim.units)
            {
                if (other.isDead() || other.unitType != unitType || other.owner == hunter.owner || sim.arePlayersAllied(hunter.owner, other.owner))
                {
                    continue;
                }
                // Each axis squared exactly and then floored to whole
                // units, as the 16.16 product shifted right by 32 is.
                auto dx = simScalarToFloat(other.position.x - hunter.position.x);
                auto dz = simScalarToFloat(other.position.z - hunter.position.z);
                auto squared = static_cast<std::int64_t>(std::floor(dx * dx)) + static_cast<std::int64_t>(std::floor(dz * dz));
                auto jitter = static_cast<std::int64_t>(randomBelow(sim.rng, static_cast<unsigned int>(std::min<std::int64_t>(squared / 2, 0xFFFFFFFFll))));
                auto score = squared - jitter;
                if (!best || score <= bestScore)
                {
                    best = otherId;
                    bestScore = score;
                }
            }
            return best;
        }
    }

    MissionScripts::MissionScripts() = default;
    MissionScripts::~MissionScripts() = default;
    MissionScripts::MissionScripts(const MissionScripts&) = default;
    MissionScripts& MissionScripts::operator=(const MissionScripts&) = default;
    MissionScripts::MissionScripts(MissionScripts&&) noexcept = default;
    MissionScripts& MissionScripts::operator=(MissionScripts&&) noexcept = default;
    bool MissionScripts::operator==(const MissionScripts& rhs) const = default;

    bool MissionScripts::isRunning(UnitId unitId) const
    {
        return scripts.count(unitId.value) != 0;
    }

    void MissionScripts::unitDamaged(UnitId unitId)
    {
        for (auto& [id, script] : scripts)
        {
            for (auto& step : script.steps)
            {
                if (step.kind == MissionStep::Kind::WaitForAttack && step.target.value_or(UnitId(id)) == unitId)
                {
                    step.hit = true;
                }
            }
        }
    }

    void MissionScripts::unitChangedOwner(UnitId unitId)
    {
        // The original's owner change kills the unit and makes a new one for
        // the captor: the new one has no list, and whoever was watching the
        // old one hears it die (event 8, 0x489740).
        scripts.erase(unitId.value);
        for (auto& [id, script] : scripts)
        {
            for (auto& step : script.steps)
            {
                if (step.kind == MissionStep::Kind::WaitForAttack && step.target == unitId)
                {
                    step.hit = true;
                }
            }
        }
    }

    void MissionScripts::update(GameSimulation& sim)
    {
        for (auto it = scripts.begin(); it != scripts.end();)
        {
            UnitId unitId(it->first);
            auto& script = it->second;
            auto unitRef = sim.tryGetUnitState(unitId);
            if (!unitRef || unitRef->get().isDead())
            {
                it = scripts.erase(it);
                continue;
            }

            // A stunned unit does nothing. One riding in a transport waits to
            // be set down, except that a computer's goes on counting its
            // waits (see runsAboard).
            const auto& unit = unitRef->get();
            auto aboard = unit.carriedBy.has_value();
            auto countsAboard = sim.getPlayer(unit.owner).type == GamePlayerType::Computer;
            if (!unit.isParalyzed(sim.gameTime) && (!aboard || countsAboard))
            {
                // A step that finishes lets the next one start on the same
                // tick; the list only ever gets shorter, so this ends.
                while (!script.steps.empty() && (!aboard || runsAboard(script.steps.front().kind)) && runHead(sim, unitId, script))
                {
                    script.steps.pop_front();
                    script.started = false;
                    if (sim.getUnitState(unitId).isDead())
                    {
                        break;
                    }
                }
            }

            if (script.steps.empty())
            {
                it = scripts.erase(it);
                continue;
            }
            ++it;
        }
    }

    bool MissionScripts::runHead(GameSimulation& sim, UnitId unitId, MissionScript& script)
    {
        using K = MissionStep::Kind;
        auto& unit = sim.getUnitState(unitId);
        auto& step = script.steps.front();

        // A move is nothing to a unit that cannot move; the original's order
        // builder gives it a QMOVE, which RWE has no use for.
        if (step.kind == K::Move && !sim.unitDefinitions.at(unit.unitType).isMobile)
        {
            return true;
        }

        if (step.kind == K::Patrol)
        {
            // A route is the run of patrol steps, closed by one more waypoint
            // where the unit stands when it sets out (0x43A020), which is
            // the shape RWE's own patrol command gives it. Its orders go
            // round for ever, and the head step stays with them: a patrol
            // is never finished, so nothing after it in the list runs (the
            // original would rotate to it; no shipped list has anything
            // there).
            if (!script.started)
            {
                if (!unit.orders.empty())
                {
                    return false;
                }
                std::size_t route = 0;
                while (route < script.steps.size() && script.steps[route].kind == K::Patrol)
                {
                    unit.orders.push_back(PatrolOrder(script.steps[route].position));
                    ++route;
                }
                unit.orders.push_back(PatrolOrder(unit.position));
                script.steps.erase(script.steps.begin() + 1, script.steps.begin() + static_cast<std::ptrdiff_t>(route));
                script.started = true;
            }
            return false;
        }

        if (isOrderStep(step.kind))
        {
            if (!script.started)
            {
                // Whatever else the unit is doing goes first.
                if (!unit.orders.empty())
                {
                    return false;
                }
                unit.orders.push_back(orderFor(step));
                script.started = true;
                return false;
            }
            return unit.orders.empty();
        }

        switch (step.kind)
        {
            case K::FactoryBuild:
                // BUILDINGBUILD counts its number down as each one is done
                // (0x402740), which the plant's own queue does here.
                if (!script.started)
                {
                    unit.modifyBuildQueue(step.unitType, step.count);
                    script.started = true;
                    return false;
                }
                return unit.buildQueue.empty();

            case K::BuildWeapon:
                sim.modifyStockpileQueue(unitId, step.count);
                return true;

            case K::Wait:
                if (step.radius == 0)
                {
                    // A plain timer, from when the wait comes to the head.
                    if (!script.started)
                    {
                        script.wakeAt = sim.gameTime + GameTime(static_cast<unsigned int>(std::max(step.ticks, 0)));
                        script.started = true;
                    }
                    return sim.gameTime >= script.wakeAt;
                }
                // A look at once, then one every 150 to 179 ticks, the budget
                // tested before each step is taken off it (0x401D66-0x401DB4).
                if (script.started && sim.gameTime < script.wakeAt)
                {
                    return false;
                }
                script.started = true;
                if (enemyWithin(sim, unit, step.radius) || step.ticks <= 0)
                {
                    return true;
                }
                {
                    auto pause = static_cast<int>(randomBelow(sim.rng, 30u)) + 150;
                    step.ticks -= pause;
                    script.wakeAt = sim.gameTime + GameTime(static_cast<unsigned int>(pause));
                }
                return false;

            case K::WaitForAttack:
            {
                auto watched = sim.tryGetUnitState(step.target.value_or(unitId));
                return step.hit || !watched || watched->get().isDead();
            }

            case K::AttackType:
            {
                // A unit that cannot attack flushes its whole list, the
                // hand-back included (0x401F8E), and stays held.
                if (!sim.unitDefinitions.at(unit.unitType).canAttack)
                {
                    script.steps.clear();
                    return false;
                }
                // The attack it pushed is still going.
                if (!unit.orders.empty())
                {
                    return false;
                }
                if (!script.started)
                {
                    script.wakeAt = sim.gameTime + GameTime(randomBelow(sim.rng, 90u) + 1u);
                    script.started = true;
                    return false;
                }
                if (sim.gameTime < script.wakeAt)
                {
                    return false;
                }
                auto target = huntTarget(sim, unit, step.unitType);
                if (!target)
                {
                    // None of them left: on to the next order.
                    return true;
                }
                unit.orders.push_back(AttackOrder(*target));
                // Back to the top once the attack is over (0x401F67).
                script.started = false;
                return false;
            }

            case K::SelfDestruct:
                // SELFDESTRUCTFG with the countdown skipped (0x40213C).
                sim.selfDestructUnit(unitId);
                return true;

            case K::MakeSelectable:
            {
                // 0x401CC0: selectable, and no longer Immune.
                unit.heldByMission = false;
                unit.immune = false;
                // A computer's unit is taken on by its AI from here
                // (0x408830), which gives it the AI's standing orders.
                if (sim.getPlayer(unit.owner).type == GamePlayerType::Computer)
                {
                    unit.moveOrders = sim.unitDefinitions.at(unit.unitType).canCapture ? UnitMovementOrders::Maneuver : UnitMovementOrders::Roam;
                    unit.fireOrders = UnitFireOrders::FireAtWill;
                }
                return true;
            }

            default:
                return true;
        }
    }
}
