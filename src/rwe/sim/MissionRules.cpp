#include "MissionRules.h"

#include <algorithm>
#include <cstdlib>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>

namespace rwe
{
    namespace
    {
        /** The seconds a result must go on holding before the game ends (g+0x39239 starts at 4). */
        constexpr int CountdownStart = 4;

        /** The Passes rules' band: a unit's cell within two of the line's (0x48F370). */
        constexpr int PassesBandCells = 2;

        /**
         * The dying unit, for the kill rules' counts. The original sends the
         * kill event before the unit leaves its owner's array (0x486799 against
         * 0x486DC7), so the counts include it: that is what the "at most one
         * left" tests mean.
         */
        struct Dying
        {
            UnitId unitId;
            PlayerId owner;
        };

        bool ownedBy(const UnitState& unit, const std::optional<PlayerId>& player)
        {
            return player && unit.owner == *player;
        }

        /**
         * How many of `player`'s units pass `test`, as the original counts them
         * by walking that player's unit array.
         *
         * A dying unit is still in its owner's array. A captured one is in
         * two: the original in its old owner's, about to die, and the copy
         * made for the captor.
         */
        template <typename Test>
        int countUnits(const GameSimulation& sim, const std::optional<PlayerId>& player, const std::optional<Dying>& dying, Test&& test)
        {
            if (!player)
            {
                return 0;
            }
            int count = 0;
            for (const auto& [unitId, unit] : sim.units)
            {
                auto isDying = dying && dying->unitId == unitId;
                if (unit.isDead() && !isDying)
                {
                    continue;
                }
                if (!test(unit))
                {
                    continue;
                }
                if (unit.owner == *player)
                {
                    ++count;
                }
                if (isDying && dying->owner == *player && unit.owner != *player)
                {
                    ++count;
                }
            }
            return count;
        }

        template <typename Test>
        bool anyUnit(const GameSimulation& sim, const std::optional<PlayerId>& player, Test&& test)
        {
            return countUnits(sim, player, std::nullopt, std::forward<Test>(test)) > 0;
        }

        bool typeMatches(const MissionRule& rule, const UnitState& unit)
        {
            return rule.unitType.empty() || rule.unitType == unit.unitType;
        }

        /**
         * The selection cursor's test, which MoveUnitToRadius and AllUnitsKilled
         * use too (0x48F283, 0x48F794): selectable, finished, and either not
         * carried or sitting on an air base. The original also wants no
         * owner-transfer lock, which RWE does not have.
         */
        bool isUsable(const GameSimulation& sim, const UnitState& unit)
        {
            if (unit.heldByMission || unit.isBeingBuilt(sim.unitDefinitions.at(unit.unitType)))
            {
                return false;
            }
            if (!unit.carriedBy)
            {
                return true;
            }
            auto carrier = sim.tryGetUnitState(*unit.carriedBy);
            return carrier && sim.unitDefinitions.at(carrier->get().unitType).isAirBase;
        }

        /**
         * The cell words at unit+0x76 and +0x78: the footprint's top-left cell,
         * rounded, which is what computeFootprintRegion gives.
         */
        Point unitCell(const GameSimulation& sim, const UnitState& unit)
        {
            auto rect = sim.computeFootprintRegion(unit.position, sim.unitDefinitions.at(unit.unitType).movementCollisionInfo);
            return Point(rect.x, rect.y);
        }

        bool isVictoryKind(MissionRule::Kind kind)
        {
            return kind <= MissionRule::Kind::VictoryTimerRunsOut;
        }

        /**
         * 0x485070: the height at a whole-unit point, bilinear over the height
         * map in sixteenths with each step truncated toward zero, and -1 off
         * the map.
         */
        int taHeightAt(const Grid<unsigned char>& heights, int x, int z)
        {
            auto cellX = x >> 4;
            auto cellZ = z >> 4;
            auto fracX = x & 0xF;
            auto fracZ = z & 0xF;
            if (cellX < 0 || cellX + 1 >= static_cast<int>(heights.getWidth()) || cellZ < 0 || cellZ + 1 >= static_cast<int>(heights.getHeight()))
            {
                return -1;
            }
            auto h = [&](int cx, int cz) { return static_cast<int>(heights.get(cx, cz)); };
            auto top = h(cellX, cellZ) + ((h(cellX + 1, cellZ) - h(cellX, cellZ)) * fracX) / 16;
            auto bottom = h(cellX, cellZ + 1) + ((h(cellX + 1, cellZ + 1) - h(cellX, cellZ + 1)) * fracX) / 16;
            return top + ((bottom - top) * fracZ) / 16;
        }
    }

    MissionRules::MissionRules() = default;
    MissionRules::~MissionRules() = default;
    MissionRules::MissionRules(const MissionRules&) = default;
    MissionRules& MissionRules::operator=(const MissionRules&) = default;
    MissionRules::MissionRules(MissionRules&&) noexcept = default;
    MissionRules& MissionRules::operator=(MissionRules&&) noexcept = default;
    bool MissionRules::operator==(const MissionRules& rhs) const = default;

    SimVector missionPointOnGround(const MapTerrain& terrain, int x, int z)
    {
        // 0x484B50. The point is one of the screen plane, where a spot on the
        // ground at height h shows h/2 further north than it is, so it walks
        // north from eight cells south of the point, a cell at a time, to the
        // first spot that shows at or above it, and then interpolates back
        // between that spot and the one before. Water counts as ground at sea
        // level.
        const auto& heights = terrain.getHeightMap();
        auto width = static_cast<int>(heights.getWidth()) * 16;
        auto height = static_cast<int>(heights.getHeight()) * 16;
        x = std::clamp(x, 0, width - 1);
        z = std::clamp(z, 0, height - 1);
        auto seaLevel = static_cast<int>(terrain.getSeaLevel().value);
        auto groundAt = [&](int pz) { return std::max(taHeightAt(heights, x, pz), seaLevel); };

        auto spot = (z & ~0xF) + 128;
        auto spotHeight = groundAt(spot);
        for (int budget = 128;; budget -= 16)
        {
            spotHeight = groundAt(spot);
            if (spot - (spotHeight >> 1) <= z)
            {
                break;
            }
            if (budget - 16 < 0)
            {
                return SimVector(intToSimScalar(x), intToSimScalar(spotHeight), intToSimScalar(spot));
            }
            spot -= 16;
        }

        auto shown = spot - (spotHeight >> 1);
        auto next = spot + 16;
        auto nextShown = next - (groundAt(next) >> 1);
        if (shown >= nextShown)
        {
            return SimVector(intToSimScalar(x), intToSimScalar(spotHeight), intToSimScalar(spot));
        }

        // In 16.16, as the original divides.
        auto fixedZ = (static_cast<int64_t>(spot) << 16) + ((static_cast<int64_t>(z - shown) << 20) / (nextShown - shown));
        auto groundZ = static_cast<int>(fixedZ >> 16);
        return SimVector(intToSimScalar(x), intToSimScalar(groundAt(groundZ)), SimScalar(static_cast<float>(fixedZ) / 65536.0f));
    }

    void MissionRules::celebrate(MissionRule& rule)
    {
        // 0x47F1A0("Victory Condition", 0), once per rule and only for victory.
        if (isVictoryKind(rule.kind))
        {
            rule.celebrated = true;
        }
    }

    unsigned int MissionRules::celebrations() const
    {
        return static_cast<unsigned int>(std::count_if(victory.begin(), victory.end(), [](const auto& r) { return r.celebrated; }));
    }

    bool MissionRules::economyFrozen() const
    {
        return countdown >= 0;
    }

    bool MissionRules::isSatisfied(const GameSimulation& sim, MissionRule& rule)
    {
        using K = MissionRule::Kind;
        switch (rule.kind)
        {
            case K::DestroyAllUnits:
            {
                // 0x48EB40: P1's unit count, nanoframes and passengers included.
                // Not latched: P1 getting a unit back makes it false again.
                if (countUnits(sim, computer, std::nullopt, [](const auto&) { return true; }) != 0)
                {
                    return false;
                }
                celebrate(rule);
                return true;
            }
            case K::BuildUnitType:
            {
                // 0x48EDB0: a finished P0 unit of the type.
                if (!rule.satisfied && anyUnit(sim, human, [&](const UnitState& u) { return typeMatches(rule, u) && !u.isBeingBuilt(sim.unitDefinitions.at(u.unitType)); }))
                {
                    rule.satisfied = true;
                    celebrate(rule);
                }
                return rule.satisfied;
            }
            case K::MoveUnitToRadius:
            {
                // 0x48F200: a usable P0 unit of the type, or of any, with its
                // centre inside the circle, flat and edge included.
                auto inside = anyUnit(sim, human, [&](const UnitState& u) {
                    if (!typeMatches(rule, u) || !isUsable(sim, u))
                    {
                        return false;
                    }
                    auto dx = u.position.x - rule.x;
                    auto dz = u.position.z - rule.z;
                    return (dx * dx) + (dz * dz) <= rule.radius * rule.radius;
                });
                if (inside)
                {
                    rule.satisfied = true;
                    celebrate(rule);
                }
                return rule.satisfied;
            }
            case K::UnitTypePassesX:
            case K::UnitTypePassesZ:
            {
                // 0x48F3E0, 0x48F530: any P0 unit of the type, or of any, in the
                // band. Not a crossing test, and nothing else about the unit is.
                if (!rule.satisfied)
                {
                    auto alongX = rule.kind == K::UnitTypePassesX;
                    auto inBand = anyUnit(sim, human, [&](const UnitState& u) {
                        auto cell = unitCell(sim, u);
                        return typeMatches(rule, u) && std::abs((alongX ? cell.x : cell.y) - rule.number) <= PassesBandCells;
                    });
                    if (inBand)
                    {
                        rule.satisfied = true;
                        celebrate(rule);
                    }
                }
                return rule.satisfied;
            }
            case K::AnyUnitPassesX:
            case K::AnyUnitPassesZ:
            {
                // 0x48FB60, 0x48FC70: any P1 unit at all in the band.
                if (!rule.satisfied)
                {
                    auto alongX = rule.kind == K::AnyUnitPassesX;
                    rule.satisfied = anyUnit(sim, computer, [&](const UnitState& u) {
                        auto cell = unitCell(sim, u);
                        return std::abs((alongX ? cell.x : cell.y) - rule.number) <= PassesBandCells;
                    });
                }
                return rule.satisfied;
            }
            case K::VictoryTimerRunsOut:
            case K::DeathTimerRunsOut:
                // 0x48F610, 0x48FD50: a bare compare. Nothing stored, no sound.
                return sim.gameTime.value >= static_cast<unsigned int>(std::max(rule.number, 0));
            case K::AllUnitsKilled:
                // 0x48F7E0: recomputed every poll. P0 is beaten once nothing it
                // has is usable, so nanoframes and passengers do not save it.
                rule.satisfied = !anyUnit(sim, human, [&](const UnitState& u) { return isUsable(sim, u); });
                return rule.satisfied;
            default:
                // The event rules: whatever the last kill or capture left.
                return rule.satisfied;
        }
    }

    void MissionRules::stepCountdown(MissionOutcome result)
    {
        // 0x465881 and 0x4650EE: the first second a result is seen starts
        // the count and does nothing else; each later one takes one off, and
        // the one that takes it below zero decides the game.
        if (countdown < 0)
        {
            countdown = CountdownStart;
            return;
        }
        --countdown;
        if (countdown < 0)
        {
            outcome = result;
        }
    }

    void MissionRules::update(const GameSimulation& sim)
    {
        if (sim.gameTime.value % SimTicksPerSecond != 0)
        {
            return;
        }
        if (!enabled || outcome)
        {
            return;
        }

        // 0x490230: all victory rules, in the builder's order, stopping at
        // the first that does not hold. A later rule is not even looked at
        // until every earlier one holds in the same poll, which matters for
        // the rules that latch on what they see.
        auto won = std::all_of(victory.begin(), victory.end(), [&](auto& rule) { return isSatisfied(sim, rule); });
        if (won)
        {
            // Victory wins a tie: defeat is not checked in a second that
            // victory holds (0x4650D5).
            stepCountdown(MissionOutcome::Victory);
            return;
        }

        // 0x490360: any defeat rule.
        auto lost = std::any_of(defeat.begin(), defeat.end(), [&](auto& rule) { return isSatisfied(sim, rule); });
        if (lost)
        {
            stepCountdown(MissionOutcome::Defeat);
        }
    }

    void MissionRules::unitDying(const GameSimulation& sim, UnitId unitId, PlayerId owner)
    {
        if (!enabled)
        {
            return;
        }

        using K = MissionRule::Kind;
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        Dying dying{unitId, owner};
        auto ofType = [](const std::string& unitType) { return [unitType](const UnitState& u) { return u.unitType == unitType; }; };

        auto visit = [&](MissionRule& rule) {
            switch (rule.kind)
            {
                case K::KillEnemyCommander:
                    // 0x48EA40: P1's own side's commander, by whatever cause.
                    if (owner == computer && unit.unitType == computerCommander)
                    {
                        rule.satisfied = true;
                        celebrate(rule);
                    }
                    break;
                case K::KillAllMobileUnits:
                    // 0x48EC20: a P1 mobile unit dies and P1 has at most one
                    // mobile unit left, this one included. A nanoframe of a
                    // mobile type is mobile.
                    if (owner == computer && unitDefinition.isMobile)
                    {
                        if (countUnits(sim, computer, dying, [&](const UnitState& u) { return sim.unitDefinitions.at(u.unitType).isMobile; }) <= 1)
                        {
                            rule.satisfied = true;
                            celebrate(rule);
                        }
                    }
                    break;
                case K::KillAllOfType:
                    // 0x48EFB0: as KillAllMobileUnits, for one type.
                    if (!rule.satisfied && owner == computer && unit.unitType == rule.unitType)
                    {
                        if (countUnits(sim, computer, dying, ofType(rule.unitType)) <= 1)
                        {
                            rule.satisfied = true;
                            celebrate(rule);
                        }
                    }
                    break;
                case K::KillUnitType:
                    // 0x48F0F0: P1 losses of the type, counted down to zero.
                    if (rule.number > 0 && owner == computer && unit.unitType == rule.unitType)
                    {
                        --rule.number;
                        if (rule.number <= 0)
                        {
                            rule.satisfied = true;
                            celebrate(rule);
                        }
                    }
                    break;
                case K::CommanderKilled:
                    // 0x48F6B0: P0's own side's commander.
                    if (owner == human && unit.unitType == humanCommander)
                    {
                        rule.satisfied = true;
                    }
                    break;
                case K::AllUnitsKilledOfType:
                    // 0x48F9D0: anyone's unit of the type, and P0's and P1's
                    // together then hold at most one, this one included. A
                    // capture leaves two, the copy and the original, so it
                    // is only destruction that loses.
                    if (unit.unitType == rule.unitType)
                    {
                        auto left = countUnits(sim, human, dying, ofType(rule.unitType)) + countUnits(sim, computer, dying, ofType(rule.unitType));
                        if (left <= 1)
                        {
                            rule.satisfied = true;
                        }
                    }
                    break;
                case K::UnitTypeKilled:
                    // 0x48F8C0: anyone's loss of the type, with no floor.
                    if (unit.unitType == rule.unitType)
                    {
                        --rule.number;
                        if (rule.number <= 0)
                        {
                            rule.satisfied = true;
                        }
                    }
                    break;
                default:
                    break;
            }
        };

        // 0x4904C0: every victory rule, then every defeat rule.
        std::for_each(victory.begin(), victory.end(), visit);
        std::for_each(defeat.begin(), defeat.end(), visit);
    }

    void MissionRules::unitChangingOwner(const GameSimulation& sim, UnitId unitId)
    {
        if (!enabled)
        {
            return;
        }

        // 0x48EEB0, CaptureUnitType's only method: a P1 unit of the type, to
        // whoever it goes.
        const auto& unit = sim.getUnitState(unitId);
        for (auto& rule : victory)
        {
            if (rule.kind == MissionRule::Kind::CaptureUnitType && ownedBy(unit, computer) && unit.unitType == rule.unitType)
            {
                rule.satisfied = true;
                celebrate(rule);
            }
        }
    }
}
