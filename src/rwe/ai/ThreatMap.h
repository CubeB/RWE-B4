#pragma once

#include <algorithm>
#include <optional>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/grid/Grid.h>
#include <rwe/grid/Point.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimVector.h>
#include <vector>

namespace rwe
{
    struct GameSimulation;
    struct AiTuningProfile;

    /**
     * Coarse influence map over the playable area, at the sim's vision cell
     * size (32 world units). Rebuilt from the blackboard's known enemies.
     *
     * - antiGround: enemy damage per second that can reach the cell.
     * - antiAirCover: how many enemy units with an anti-air weapon reach the cell.
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

        /**
         * How many enemy units that can shoot at aircraft cover this spot.
         * A count and not a damage figure on purpose: what decides whether a
         * bombing run is worth making is how many things will be firing at
         * the bomber, and a count is a thing the profile can name a limit for
         * in the words a player would use.
         */
        float antiAirCoverAt(const SimVector& position) const;
        float economicAt(const SimVector& position) const;
        float antiGroundInRadius(const SimVector& position, float radiusWorldUnits) const;

        /** Cell centre that has gone longest without being seen, weighted against travel distance. */
        std::optional<SimVector> bestScoutTarget(const SimVector& from) const;

        /**
         * As above, considering only cells the predicate accepts (given the
         * cell and its centre).
         *
         * A template rather than a std::function because this runs over every
         * cell on the map. The predicate is only asked about cells that would
         * beat the best score so far - the cells it rejects could not have
         * been picked anyway - so it can afford to be expensive.
         */
        template <typename Accept>
        std::optional<SimVector> bestScoutTarget(const SimVector& from, Accept&& accept) const
        {
            std::optional<SimVector> best;
            float bestScore = -1.0f;
            auto width = getWidth();
            auto height = getHeight();
            const auto& stalenessCells = staleness.getVector();
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    auto stale = stalenessCells[(y * width) + x];
                    if (stale <= 0.0f)
                    {
                        continue;
                    }
                    auto center = cellCenter(x, y);
                    auto distance = std::max(1.0f, (center - from).length().value);
                    // Prefer ground that has gone unseen for a long time, but not at any distance.
                    auto score = std::min(stale, 3000.0f) / distance;
                    if (score <= bestScore)
                    {
                        continue;
                    }
                    if (!accept(x, y, center))
                    {
                        continue;
                    }
                    bestScore = score;
                    best = center;
                }
            }
            return best;
        }

        float antiGroundAtCell(int x, int y) const { return antiGround.get(x, y); }

        /** The enemy cell worth attacking most: value minus threat. */
        std::optional<SimVector> bestAttackTarget(float threatAversion) const;

        SimVector cellCenter(int x, int y) const;
        Point cellAt(const SimVector& position) const;

        const Grid<float>& getAntiGround() const { return antiGround; }
        const Grid<float>& getAntiAirCover() const { return antiAirCover; }
        const Grid<float>& getEconomic() const { return economic; }
        const Grid<float>& getStaleness() const { return staleness; }

    private:
        Grid<float> antiGround;
        Grid<float> antiAirCover;
        Grid<float> economic;
        Grid<float> staleness;
        /** Cells the last rebuild put economic value in, in map scan order. */
        std::vector<std::size_t> economicCellIndices;
        /** Cells the last rebuild put threat in, so only those need clearing. May repeat. */
        std::vector<std::size_t> antiGroundCellIndices;
        /** As above, for the anti-air layer. May repeat. */
        std::vector<std::size_t> antiAirCoverCellIndices;
        SimVector origin{0_ss, 0_ss, 0_ss};
        float cellSize{32.0f};
    };
}
