#pragma once

// Where the simulation hands the events a live sink is told about.

#include <optional>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    struct GameSimulation;

    /**
     * The events a live sink is told about, as function pointers over an
     * opaque context. `TaLiveSender::hooks()` fills one.
     *
     * **A table rather than a pointer to the sink, and the reason is measured.**
     * `GameSimulation.h` is included by most of the engine, and a
     * `std::unique_ptr` to a type named there costs every one of those
     * translation units the deleter's worth of COFF sections: 55 on
     * `GameSimulation.cpp` at `-O0 -g1` for a member that held nothing at all,
     * which is twenty-seven COMDATs of `__uniq_ptr_data` and its tuple. That is
     * the budget CLAUDE.md's "Other hazards" section is about, and the six
     * calls below cost a further 26.
     *
     * So the sink belongs to whoever made it -- the network layer -- and the
     * simulation is only told where to call. Two things follow that are worth
     * having in their own right: the simulation owns no network object and so
     * can be torn down in either order, and a game with no peer attached pays
     * nothing. The price is that the two must agree on the order, so whoever
     * registers a sink unregisters it before dropping it:
     * `simulation.setTaLiveSender(sender.hooks())` and, to stop,
     * `simulation.setTaLiveSender({})`.
     *
     * A field of a live table is never null, so `attached()` is the question
     * the simulation asks before every call.
     */
    struct TaLiveSenderHooks
    {
        void* context{nullptr};

        void (*unitCreated)(void* context, const GameSimulation& simulation, UnitId unit){nullptr};
        void (*unitRemoved)(void* context, UnitId unit){nullptr};
        void (*buildStarted)(void* context, const GameSimulation& simulation, UnitId builder, UnitId unit){nullptr};
        void (*shotFired)(
            void* context,
            const GameSimulation& simulation,
            UnitId shooter,
            unsigned int weaponSlot,
            std::optional<UnitId> targetUnit,
            const SimVector& origin,
            const SimVector& aimPoint,
            const SimVector& direction){nullptr};
        void (*unitCaptured)(void* context, const GameSimulation& simulation, UnitId unit, PlayerId newOwner){nullptr};
        void (*endOfTick)(void* context, const GameSimulation& simulation){nullptr};

        /** An aim script started, which TA replicates so the peer turns the same piece (a 0x10). */
        void (*aimScriptStarted)(void* context, UnitId unit, unsigned int functionIndex, int heading, int pitch){nullptr};

        /** Whether a live sink is listening. */
        bool attached() const { return endOfTick != nullptr; }
    };
}
