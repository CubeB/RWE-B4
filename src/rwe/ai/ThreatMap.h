#pragma once

#include <functional>
#include <optional>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/grid/Grid.h>
#include <rwe/grid/Point.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimVector.h>

namespace rwe
{
    struct GameSimulation;
    struct AiTuningProfile;

    /**
     * Coarse influence map over the playable area, at the sim's vision cell
     * size (32 world units). Rebuilt from the blackboard's known enemies.
     *
     * - antiGround: enemy damage per second that can reach the cell.
     * - economic:   metal value of enemy buildings in the cell.
     * - staleness:  ticks since the cell was last in our line of sight.
     */
    class ThreatMap
    {
    public:
        ThreatMap() = default;
        ThreatMap(int width, int height);

        void rebuild(const GameSimulation& sim, PlayerId aiOwner, const AiBlackboard& bb, bool omniscient);

        int getWidth() const { return antiGround.getWidth(); }
        int getHeight() const { return antiGround.getHeight(); }
        bool isEmpty() const { return getWidth() == 0 || getHeight() == 0; }

        float antiGroundAt(const SimVector& position) const;
        float economicAt(const SimVector& position) const;
        float antiGroundInRadius(const SimVector& position, float radiusWorldUnits) const;

        /** Cell centre that has gone longest without being seen, weighted against travel distance. */
        std::optional<SimVector> bestScoutTarget(const SimVector& from) const;

        /** As above, considering only cells the predicate accepts (given the cell and its centre). */
        std::optional<SimVector> bestScoutTarget(const SimVector& from, const std::function<bool(int, int, const SimVector&)>& accept) const;

        float antiGroundAtCell(int x, int y) const { return antiGround.get(x, y); }

        /** The enemy cell worth attacking most: value minus threat. */
        std::optional<SimVector> bestAttackTarget(float threatAversion) const;

        SimVector cellCenter(int x, int y) const;
        Point cellAt(const SimVector& position) const;

        const Grid<float>& getAntiGround() const { return antiGround; }
        const Grid<float>& getEconomic() const { return economic; }
        const Grid<float>& getStaleness() const { return staleness; }

    private:
        Grid<float> antiGround;
        Grid<float> economic;
        Grid<float> staleness;
        SimVector origin{0_ss, 0_ss, 0_ss};
        float cellSize{32.0f};
    };
}
