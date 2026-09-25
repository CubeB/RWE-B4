#pragma once

#include <deque>
#include <map>
#include <optional>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <string>

namespace rwe
{
    struct GameSimulation;

    /**
     * One entry of a mission unit's order list, as the InitialMission
     * interpreter 0x487BF0 queued it (TOTALA-EXE-DATA.md §114). Names are
     * already resolved: a step that named something that did not exist was
     * never queued, as in the original.
     */
    struct MissionStep
    {
        enum class Kind
        {
            /** m: an ordinary move. */
            Move,
            /** p: a patrol, which never ends. */
            Patrol,
            /** a X Z: an attack on the ground, which never ends. */
            AttackPoint,
            /** a NAME: every enemy unit of the type, nearest-ish first (0x401E00). */
            AttackType,
            /** g NAME: guard the unit, until it dies. */
            Guard,
            /** u: unload at the point. */
            Unload,
            /** b NAME x z from a mobile builder: build it there. */
            Build,
            /** b NAME n from a plant: n of them from the queue. */
            FactoryBuild,
            /** bw n: stockpile n missiles; nothing to wait for. */
            BuildWeapon,
            /** w N [R]: N seconds, or until a seen enemy comes within R (0x401CE0). */
            Wait,
            /** wa [NAME]: until the watched unit is hit or dies (0x401FD0). */
            WaitForAttack,
            /** d: blow up, at once. */
            SelfDestruct,
            /** s, or the one appended: hand the unit to the player (0x401CC0). */
            MakeSelectable,
        };

        Kind kind{Kind::MakeSelectable};

        /** Where a point order goes, in world coordinates. */
        SimVector position{0_ss, 0_ss, 0_ss};

        /** The unit type an AttackType, Build or FactoryBuild names, upper case. */
        std::string unitType;

        /** Guard's unit, or the one WaitForAttack watches. */
        std::optional<UnitId> target;

        /** FactoryBuild's and BuildWeapon's count. */
        int count{0};

        /** Wait: the budget left in ticks (`+0x36`), and the radius in world units (`+0x3A`). */
        int ticks{0};
        int radius{0};

        /**
         * WaitForAttack: the watched unit has been hit. Latched from the
         * moment the step is queued, as the original's watcher is: an event
         * ORs into the mission whether or not it is at the head yet.
         */
        bool hit{false};

        bool operator==(const MissionStep& rhs) const = default;
    };

    /** A mission unit's list, and where the step at its head has got to. */
    struct MissionScript
    {
        std::deque<MissionStep> steps;

        /** The head step has begun: its order is out, or its timer set. */
        bool started{false};

        /** When a Wait or an AttackType looks again. */
        GameTime wakeAt{0};

        bool operator==(const MissionScript& rhs) const = default;
    };

    /**
     * Every mission unit's order list, driven from the simulation's tick.
     *
     * The list sits above the unit's own order queue rather than in it: a
     * step that is an ordinary order (move, patrol, attack, guard, unload,
     * build) is handed to the queue when its turn comes, and the next step
     * waits for the queue to empty. The steps the original runs as missions
     * of their own -- the waits, the hunt, the hand-back -- are run here.
     * That keeps the unit's order variant as it is, which every translation
     * unit that touches a unit pays for.
     */
    struct MissionScripts
    {
        /** By raw unit id, which carries the slot's generation, so a dead unit's script can never pass to its slot's next occupant. */
        std::map<unsigned int, MissionScript> scripts;

        MissionScripts();
        ~MissionScripts();
        MissionScripts(const MissionScripts&);
        MissionScripts& operator=(const MissionScripts&);
        MissionScripts(MissionScripts&&) noexcept;
        MissionScripts& operator=(MissionScripts&&) noexcept;

        bool operator==(const MissionScripts& rhs) const;

        /** Runs every script one tick, in unit id order. Before the behaviour pass. */
        void update(GameSimulation& sim);

        /** A unit took damage: any WaitForAttack watching it is over when its turn comes. */
        void unitDamaged(UnitId unitId);

        /**
         * Whether the unit's script still has anything to do. A unit whose
         * queue is empty but whose script is waiting is not idle: it does not
         * go looking for a fight or a landing pad.
         */
        bool isRunning(UnitId unitId) const;

    private:
        /** Runs the head step. True when it is finished and the next one should start. */
        bool runHead(GameSimulation& sim, UnitId unitId, MissionScript& script);
    };
}
