#pragma once

#include <chrono>
#include <map>
#include <string>

namespace rwe
{
    // Per-phase frame timing scaffolding for the renderer, compiled out by
    // default, and deliberately the same shape as sim/sim_prof.h so the two
    // breakdowns can be read side by side: GameScene::render accumulates into
    // these and prints a RENDERPROF line every two seconds, exactly as
    // GameSimulation::tick prints SIMPROF.
    //
    // It lives in a header rather than in GameScene.cpp because the phases
    // worth timing are spread across GameScene, RenderService and
    // SceneManager, and one reporting mechanism is enough.
    //
    // The report zeroes the slots rather than clearing the map, so a
    // reference taken once (a function-local static at a call site) stays
    // valid for the life of the process. std::map never invalidates
    // references on insert, so taking them is safe even as later slots
    // appear.
    inline std::map<std::string, double> renderProfTotals;

    // Counters rather than timings: draw calls, vertices, batch sizes. A
    // millisecond figure says a phase is expensive; the count says whether it
    // is expensive because of how much it draws or how often it asks.
    inline std::map<std::string, double> renderProfCounts;

    inline double& renderProfSlot(const char* name)
    {
        return renderProfTotals[name];
    }

    inline double& renderProfCountSlot(const char* name)
    {
        return renderProfCounts[name];
    }

    /**
     * Times a scope into a slot, via RWE_RENDERPROF below. The clock reads
     * are the whole cost, a few tens of nanoseconds for the pair, which is
     * small against a render phase but not against a per-piece push_back --
     * so put these around phases and loops rather than inside them, and read
     * anything under a microsecond a frame with that in mind.
     *
     * One to a scope: the macro names its variables.
     */
    class RenderProfTimer
    {
    public:
        explicit RenderProfTimer(double& slot)
            : slot(&slot), start(std::chrono::steady_clock::now())
        {
        }

        ~RenderProfTimer()
        {
            *slot += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }

        RenderProfTimer(const RenderProfTimer&) = delete;
        RenderProfTimer& operator=(const RenderProfTimer&) = delete;

    private:
        double* slot;
        std::chrono::steady_clock::time_point start;
    };
}

// Off unless the build asks for it, for the same reason the sim timers are:
// the clock pair is a real cost next to the work at some of these call sites,
// and a profiler that taxes what it measures is worse than none. Build with
// -DRWE_ENABLE_RENDERPROF to turn the frame breakdown on. The call sites stay
// in the code because the next round of performance work will want exactly
// this breakdown.
#ifdef RWE_ENABLE_RENDERPROF
#define RWE_RENDERPROF(name)                                          \
    static double& rweRenderProfSlot_ = ::rwe::renderProfSlot(name); \
    ::rwe::RenderProfTimer rweRenderProfTimer_(rweRenderProfSlot_)
#define RWE_RENDERPROF_COUNT(name, n)                                            \
    do                                                                           \
    {                                                                            \
        static double& rweRenderProfCount_ = ::rwe::renderProfCountSlot(name);   \
        rweRenderProfCount_ += static_cast<double>(n);                           \
    } while (false)
#else
#define RWE_RENDERPROF(name) ((void)0)
#define RWE_RENDERPROF_COUNT(name, n) ((void)0)
#endif
