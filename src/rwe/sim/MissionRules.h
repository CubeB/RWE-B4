#pragma once

#include <optional>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <string>
#include <vector>

namespace rwe
{
    struct GameSimulation;
    class MapTerrain;

    /**
     * One of a campaign mission's win or lose conditions, as the builder at
     * 0x48E010 makes it from the mission's [GlobalHeader]. The eighteen
     * kinds, what each one tests and every quirk kept here are in
     * TOTALA-EXE-DATA.md §113.
     *
     * P0 below is the player in slot 0 (`Player=1` in the file, the human)
     * and P1 the player in slot 1 (`Player=2`, the computer). The rules know
     * no other players and no alliances.
     */
    struct MissionRule
    {
        enum class Kind
        {
            // Victory, in the builder's order.
            KillEnemyCommander,
            DestroyAllUnits,
            KillAllMobileUnits,
            BuildUnitType,
            CaptureUnitType,
            KillAllOfType,
            KillUnitType,
            MoveUnitToRadius,
            UnitTypePassesX,
            UnitTypePassesZ,
            VictoryTimerRunsOut,
            // Defeat.
            CommanderKilled,
            AllUnitsKilled,
            AllUnitsKilledOfType,
            UnitTypeKilled,
            DeathTimerRunsOut,
            AnyUnitPassesX,
            AnyUnitPassesZ,
        };

        Kind kind{Kind::DestroyAllUnits};

        /** The unit type the rule names, upper case. Empty for ANYTYPE and for the rules that name none. */
        std::string unitType;

        /**
         * KillUnitType's and UnitTypeKilled's count still to go (the save's
         * NumLeftToKill), the Passes rules' cell (N >> 4), the timers' tick
         * (N * 30).
         */
        int number{0};

        /**
         * MoveUnitToRadius's circle: the centre on the ground, in world
         * coordinates (see missionPointOnGround), and the radius.
         */
        SimScalar x{0.0f};
        SimScalar z{0.0f};
        SimScalar radius{0.0f};

        /** The rule has been met. Most kinds latch it; see isSatisfied. */
        bool satisfied{false};

        /** A victory rule has played "Victory Condition", which it does once. */
        bool celebrated{false};

        bool operator==(const MissionRule& rhs) const = default;
    };

    /**
     * 0x484B50: the spot on the ground under MoveUnitToRadius's point, in
     * world units from the map's top-left corner.
     *
     * The mission's X and Z are not a map position. They are a point of the
     * screen plane, where the camera looks down at an angle and a spot at
     * height h shows h/2 further north than it is, and the original finds the
     * ground under that point the way it finds the ground under the cursor.
     * On a flat map at height h the circle is h/2 south of where the numbers
     * say. X and Z are clamped to the map first, and water counts as ground at
     * sea level.
     */
    SimVector missionPointOnGround(const MapTerrain& terrain, int x, int z);

    enum class MissionOutcome
    {
        Victory,
        Defeat,
    };

    /**
     * The mission rules manager at [g+0x391ED]: the victory rules, which must
     * all hold, the defeat rules, any one of which loses, and the five-second
     * countdown between a result being seen and the game ending.
     *
     * The simulation holds one only in a mission, and a mission's rules
     * replace the skirmish ones outright: no commander death ends it and
     * computeWinStatus answers from `outcome` alone.
     */
    struct MissionRules
    {
        std::vector<MissionRule> victory;
        std::vector<MissionRule> defeat;

        /**
         * `+0x88`. Cleared for good when the mission has no [units]
         * (0x488547), which leaves a mission that cannot be won or lost.
         */
        bool enabled{true};

        /**
         * The word at g+0x39239: -1 until a result is seen, then 4, and one
         * lower each second the result still holds. The game ends when it
         * goes below zero again. Shared by both results and never reset.
         */
        int countdown{-1};

        std::optional<MissionOutcome> outcome;

        /** P0 and P1. Either may be absent, and then it simply has no units. */
        std::optional<PlayerId> human;
        std::optional<PlayerId> computer;

        /**
         * The commander of each one's side, upper case: what
         * KillEnemyCommander and CommanderKilled compare a dying unit's type
         * against (g+0x37F5F + side * 0x232).
         */
        std::string humanCommander;
        std::string computerCommander;

        MissionRules();
        ~MissionRules();
        MissionRules(const MissionRules&);
        MissionRules& operator=(const MissionRules&);
        MissionRules(MissionRules&&) noexcept;
        MissionRules& operator=(MissionRules&&) noexcept;

        bool operator==(const MissionRules& rhs) const;

        /**
         * The once-a-second check (0x464F80 on the local player's settle
         * tick): victory first, and defeat only when victory does not hold.
         * Does nothing on any other tick.
         */
        void update(const GameSimulation& sim);

        /**
         * A unit is about to die (0x4904C0, from the death handler 0x4866D0),
         * whatever the cause. The unit must still be in the simulation and
         * still owned by `owner`, except in the capture case below.
         *
         * A capture is two events in the original: the owner change, then a
         * cause-4 death of the old unit after its replacement has been made
         * for the captor. RWE changes the owner in place, so for a capture the
         * caller passes the old owner here after the change, and the unit is
         * counted twice: as the dying original for `owner` and as the copy
         * for its new one.
         */
        void unitDying(const GameSimulation& sim, UnitId unitId, PlayerId owner);

        /** A unit is about to change owner (0x490520). Only CaptureUnitType listens. */
        void unitChangingOwner(const GameSimulation& sim, UnitId unitId);

        /** Whether the once-a-second economy settle is held back (0x46554F): only while the countdown runs. */
        bool economyFrozen() const;

        /** How many victory rules have played "Victory Condition" so far. The scene plays one for each new one. */
        unsigned int celebrations() const;

    private:
        bool isSatisfied(const GameSimulation& sim, MissionRule& rule);
        void celebrate(MissionRule& rule);
        void stepCountdown(MissionOutcome result);
    };
}
