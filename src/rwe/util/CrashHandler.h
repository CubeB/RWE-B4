#pragma once

// Post-mortem crash reporting.
//
// Before this existed, a segfault or an access violation killed the game
// silently: the top-level catch in main.cpp only ever saw thrown
// std::exceptions, and everything that actually crashed walked straight past
// it. Recovering anything meant reproducing the fault a second time under
// tools/crash-catch.cmd, which had to have been started beforehand.
//
// Now a fatal fault writes rwe-crash-<timestamp>.txt next to rwe.log: the
// signal, the faulting address, a backtrace, and what the game was doing at
// the time.
//
// Everything below the install function runs in a broken process, so it obeys
// the signal-handler rules: no allocation, no locks, no iostreams, no
// std::filesystem, nothing that could already have been freed. That is why the
// context is fixed buffers and atomics rather than std::string, and why the
// output path is formatted once at install time.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>

namespace rwe
{
    /**
     * Where in the frame the fault happened. This is the first thing you want
     * to know about a crash and the cheapest to record: one relaxed store at
     * a handful of points in the scene loop.
     */
    enum class CrashPhase : uint32_t
    {
        Startup,
        Input,
        Update,
        SimTick,
        Render,
        Swap,
        Shutdown,
    };

    const char* crashPhaseName(CrashPhase p);

    /**
     * What the game was doing, refreshed as it runs and read back from the
     * signal handler. Fixed-size and lock-free by necessity -- see the note
     * at the top of this file.
     */
    struct CrashContext
    {
        static constexpr size_t NameSize = 64;

        std::atomic<uint32_t> phase{static_cast<uint32_t>(CrashPhase::Startup)};
        std::atomic<uint32_t> sceneTime{0};
        std::atomic<uint32_t> gameTime{0};
        std::atomic<uint32_t> unitCount{0};
        std::atomic<uint32_t> playerCount{0};

        // Written by the game thread, read by the handler. The write is a
        // copy followed by a terminator and the buffer starts terminated, so
        // the worst a racing reader sees is a truncated name -- which beats
        // the alternative of chasing a pointer that may already be freed.
        char scene[NameSize]{"none"};
        char map[NameSize]{"none"};
    };

    CrashContext& crashContext();

    void setCrashPhase(CrashPhase p);
    void setCrashScene(const char* name);
    void setCrashMap(const char* name);
    /** unitCount is VectorMap::slotCount: an upper bound, not a live count. */
    void setCrashTick(uint32_t sceneTime, uint32_t gameTime, uint32_t unitCount, uint32_t playerCount);

    /**
     * Renders the report header -- everything but the stack frames, which the
     * platform code appends afterwards. Pure and allocation-free so that it
     * can be tested without crashing anything. Always leaves buf terminated,
     * truncating rather than overrunning, and returns the length written
     * (excluding the terminator).
     */
    size_t formatCrashReport(
        char* buf,
        size_t bufSize,
        const char* faultName,
        const void* faultAddress,
        const CrashContext& ctx);

    /**
     * Builds "<dir>/rwe-crash-YYYYmmdd-HHMMSS.txt". Returns false (and leaves
     * buf terminated) if it would not fit. Split out from the install so the
     * naming can be tested directly.
     */
    bool formatCrashFilePath(char* buf, size_t bufSize, const char* dir, std::time_t when);

    /**
     * Installs handlers for the fatal signals. Formats the crash file path up
     * front -- the handler must not touch std::filesystem -- so this needs the
     * local data path, and should be called as soon as that is known.
     */
    void installCrashHandler(const std::filesystem::path& localDataPath);

    /** The path a crash would be written to, or "" if not installed. */
    const char* crashFilePath();

    /**
     * True the first time, false ever after: a fault raised inside the handler
     * must bail out rather than recurse. Exposed for the tests.
     */
    bool claimCrashHandlerEntry();
    void resetCrashHandlerEntryForTesting();
}
