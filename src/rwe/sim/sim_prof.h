#pragma once

#include <chrono>
#include <map>
#include <string>

namespace rwe
{
    // Per-phase tick timing scaffolding, compiled out by default. GameSimulation::tick
    // accumulates into these and prints a SIMPROF line every two seconds.
    // It lives in a header rather than in GameSimulation.cpp so the behaviour
    // pass can add counters of its own without a second reporting mechanism.
    //
    // The report zeroes the slots rather than clearing the map, so a
    // reference taken once (a function-local static at a call site) stays
    // valid for the life of the process. std::map never invalidates
    // references on insert, so taking them is safe even as later slots appear.
    inline std::map<std::string, double> simProfTotals;

    inline double& simProfSlot(const char* name)
    {
        return simProfTotals[name];
    }

    /**
     * Times a scope into a slot, via RWE_SIMPROF below. The clock reads are
     * the whole cost, a few tens of nanoseconds for the pair, which is small
     * against the things worth measuring but not against a per-unit call
     * that does nothing -- so put these where the work is rather than on
     * every branch, and read anything under a microsecond a call with that
     * in mind.
     *
     * One to a scope: the macro names its variables.
     */
    class SimProfTimer
    {
    public:
        explicit SimProfTimer(double& slot)
            : slot(&slot), start(std::chrono::steady_clock::now())
        {
        }

        ~SimProfTimer()
        {
            *slot += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }

        SimProfTimer(const SimProfTimer&) = delete;
        SimProfTimer& operator=(const SimProfTimer&) = delete;

    private:
        double* slot;
        std::chrono::steady_clock::time_point start;
    };
}

// Off unless the build asks for it. The timers cost a clock pair each and
// the call sites sit inside per-unit work, so leaving them live would be a
// measurable tax on the very thing they measure. Build with
// -DRWE_ENABLE_SIMPROF to turn the phase timings back on; the call sites
// stay in the code because the next round of performance work will want
// exactly the breakdown that found the two hotspots this time.
#ifdef RWE_ENABLE_SIMPROF
#define RWE_SIMPROF(name)                                      \
    static double& rweSimProfSlot_ = ::rwe::simProfSlot(name); \
    ::rwe::SimProfTimer rweSimProfTimer_(rweSimProfSlot_)
#else
#define RWE_SIMPROF(name) ((void)0)
#endif
