#pragma once

#include <functional>
#include <map>
#include <rwe/sim/UnitId.h>
#include <vector>

namespace rwe
{
    /**
     * The original computer player's squads (TOTALA-EXE-COMPUTER-PLAYER.md
     * S:119 "Classification" and S:122), kept free of the simulation so the
     * classifier and the rebalance rule can be tested on bare numbers.
     */
    namespace ClassicGroup
    {
        constexpr int None = 0;
        /** Unarmed building-class units: the factories among them. */
        constexpr int Buildings = 1;
        constexpr int LandAssault = 2;
        constexpr int LandReserve = 3;
        /** Mobile builders. */
        constexpr int Builders = 4;
        /** Armed building-class units; the assault's first choice of rally point. */
        constexpr int Defences = 5;
        constexpr int WaterAssault = 6;
        constexpr int WaterReserve = 7;
        /** Every aircraft, armed or not. */
        constexpr int Aircraft = 8;
        /** Nothing in the ordinary classifier ever puts a unit here. */
        constexpr int Probe = 9;
        constexpr int Count = 10;
    }

    /** What the classifier asks of a unit's type. */
    struct ClassicClassifyFacts
    {
        /** The building-class runtime flag: it does not move. */
        bool building{false};
        bool armed{false};
        bool builder{false};
        bool canFly{false};
        /** `MinWaterDepth > 0`. */
        bool waterDepthPositive{false};
    };

    /**
     * First match wins, and the order is the finding: building-class armed ->
     * 5, building-class unarmed -> 1, builder -> 4 (so an aircraft builder is
     * 4), canfly -> 8 (so every other aircraft is 8), MinWaterDepth > 0 -> 7,
     * armed -> 3. Anything else is 0 and is looked at again next sweep.
     */
    int classicClassify(const ClassicClassifyFacts& facts);

    /**
     * Membership by group, with the original's swap-removal: a leaver is
     * replaced by the last element, so membership order is not stable and
     * anything that takes "the first member" depends on it.
     */
    class ClassicGroups
    {
    public:
        /** Puts a unit in a group, taking it out of any other first. */
        void assign(UnitId unit, int group);

        /** Takes a unit out of whichever group it is in. Returns false if it was in none. */
        bool remove(UnitId unit);

        int groupOf(UnitId unit) const;

        const std::vector<UnitId>& members(int group) const;

        /** The unit at `index` of `group` moves to the back of `target`, the last member filling the hole. */
        UnitId transferAt(int group, std::size_t index, int target);

        /** Every unit currently in some group, in id order. */
        std::vector<UnitId> allUnits() const;

        void clear();

    private:
        std::vector<UnitId> byGroup[ClassicGroup::Count];
        std::map<unsigned int, int> groupOfUnit;
    };

    struct ClassicPosition
    {
        double x{0.0};
        double z{0.0};
    };

    /**
     * Assault rebalance against the reserve (S:122), for one of the two
     * assault/reserve pairs. `limit` is 20000 for land and 50000 for water.
     *
     * An empty assault takes the reserve's first unit. Then, repeatedly, the
     * assault's most distant member -- squared distance from the centroid at
     * or above `limit * groupSize` -- goes back to the reserve, always keeping
     * one; then reserve members inside the same criterion are taken in.
     *
     * The centroid is a running sum, and so is the source's quirk, which is
     * kept: a leaver's position is subtracted AFTER the removal has moved the
     * last member into its place, so the subtraction names that replacement.
     * (When the leaver was itself last there is no replacement and its own
     * position is subtracted. [open])
     */
    void classicRebalanceAssault(
        ClassicGroups& groups,
        int assaultGroup,
        int reserveGroup,
        double limit,
        const std::function<ClassicPosition(UnitId)>& positionOf);

    /** The mean position of the members of a group, if it has any. */
    bool classicCentroid(const ClassicGroups& groups, int group, const std::function<ClassicPosition(UnitId)>& positionOf, ClassicPosition& out);
}
