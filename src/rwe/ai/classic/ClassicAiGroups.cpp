#include "ClassicAiGroups.h"

#include <algorithm>

namespace rwe
{
    int classicClassify(const ClassicClassifyFacts& facts)
    {
        // TOTALA-EXE-COMPUTER-PLAYER.md S:119 "Classification".
        if (facts.building)
        {
            return facts.armed ? ClassicGroup::Defences : ClassicGroup::Buildings;
        }
        if (facts.builder)
        {
            return ClassicGroup::Builders;
        }
        if (facts.canFly)
        {
            return ClassicGroup::Aircraft;
        }
        if (facts.waterDepthPositive)
        {
            return ClassicGroup::WaterReserve;
        }
        if (facts.armed)
        {
            return ClassicGroup::LandReserve;
        }
        return ClassicGroup::None;
    }

    void ClassicGroups::assign(UnitId unit, int group)
    {
        remove(unit);
        if (group <= ClassicGroup::None || group >= ClassicGroup::Count)
        {
            return;
        }
        byGroup[group].push_back(unit);
        groupOfUnit[unit.value] = group;
    }

    bool ClassicGroups::remove(UnitId unit)
    {
        auto it = groupOfUnit.find(unit.value);
        if (it == groupOfUnit.end())
        {
            return false;
        }
        auto& list = byGroup[it->second];
        auto at = std::find(list.begin(), list.end(), unit);
        if (at != list.end())
        {
            // Swap-removal: the last member fills the hole.
            *at = list.back();
            list.pop_back();
        }
        groupOfUnit.erase(it);
        return true;
    }

    int ClassicGroups::groupOf(UnitId unit) const
    {
        auto it = groupOfUnit.find(unit.value);
        return it == groupOfUnit.end() ? ClassicGroup::None : it->second;
    }

    const std::vector<UnitId>& ClassicGroups::members(int group) const
    {
        static const std::vector<UnitId> empty;
        if (group < 0 || group >= ClassicGroup::Count)
        {
            return empty;
        }
        return byGroup[group];
    }

    UnitId ClassicGroups::transferAt(int group, std::size_t index, int target)
    {
        auto& list = byGroup[group];
        UnitId unit = list[index];
        list[index] = list.back();
        list.pop_back();
        byGroup[target].push_back(unit);
        groupOfUnit[unit.value] = target;
        return unit;
    }

    std::vector<UnitId> ClassicGroups::allUnits() const
    {
        std::vector<UnitId> out;
        out.reserve(groupOfUnit.size());
        for (const auto& [id, group] : groupOfUnit)
        {
            (void)group;
            out.push_back(UnitId(id));
        }
        return out;
    }

    void ClassicGroups::clear()
    {
        for (auto& list : byGroup)
        {
            list.clear();
        }
        groupOfUnit.clear();
    }

    namespace
    {
        double distanceSquared(const ClassicPosition& a, double cx, double cz)
        {
            double dx = a.x - cx;
            double dz = a.z - cz;
            return dx * dx + dz * dz;
        }
    }

    void classicRebalanceAssault(
        ClassicGroups& groups,
        int assaultGroup,
        int reserveGroup,
        double limit,
        const std::function<ClassicPosition(UnitId)>& positionOf)
    {
        // An empty assault takes the reserve's first unit.
        if (groups.members(assaultGroup).empty() && !groups.members(reserveGroup).empty())
        {
            groups.transferAt(reserveGroup, 0, assaultGroup);
        }

        double sumX = 0.0;
        double sumZ = 0.0;
        for (auto id : groups.members(assaultGroup))
        {
            auto p = positionOf(id);
            sumX += p.x;
            sumZ += p.z;
        }

        // Shed the most distant member while it is beyond the cohesion
        // criterion, always keeping one.
        while (groups.members(assaultGroup).size() > 1)
        {
            const auto& members = groups.members(assaultGroup);
            const double n = static_cast<double>(members.size());
            const double cx = sumX / n;
            const double cz = sumZ / n;

            std::size_t farthest = 0;
            double farthestD2 = -1.0;
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                double d2 = distanceSquared(positionOf(members[i]), cx, cz);
                if (d2 > farthestD2)
                {
                    farthestD2 = d2;
                    farthest = i;
                }
            }
            if (!(farthestD2 >= limit * n))
            {
                break;
            }

            const auto leaving = groups.transferAt(assaultGroup, farthest, reserveGroup);
            // The source's quirk: the subtraction reads the slot AFTER the swap,
            // so it names the replacement, not the leaver.
            const auto& after = groups.members(assaultGroup);
            const auto named = farthest < after.size() ? after[farthest] : leaving;
            auto p = positionOf(named);
            sumX -= p.x;
            sumZ -= p.z;
        }

        // Then take in reserve members inside the same criterion.
        for (std::size_t j = 0; j < groups.members(reserveGroup).size();)
        {
            const auto size = groups.members(assaultGroup).size();
            if (size == 0)
            {
                break;
            }
            const double n = static_cast<double>(size);
            const double cx = sumX / n;
            const double cz = sumZ / n;
            auto id = groups.members(reserveGroup)[j];
            auto p = positionOf(id);
            if (distanceSquared(p, cx, cz) < limit * n)
            {
                groups.transferAt(reserveGroup, j, assaultGroup);
                sumX += p.x;
                sumZ += p.z;
            }
            else
            {
                ++j;
            }
        }
    }

    bool classicCentroid(const ClassicGroups& groups, int group, const std::function<ClassicPosition(UnitId)>& positionOf, ClassicPosition& out)
    {
        const auto& members = groups.members(group);
        if (members.empty())
        {
            return false;
        }
        double sx = 0.0;
        double sz = 0.0;
        for (auto id : members)
        {
            auto p = positionOf(id);
            sx += p.x;
            sz += p.z;
        }
        out.x = sx / static_cast<double>(members.size());
        out.z = sz / static_cast<double>(members.size());
        return true;
    }
}
