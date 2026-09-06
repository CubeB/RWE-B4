// Crashes on purpose. Only a real fault exercises the signal handling, the
// alternate stack and the platform backtrace, so the crash handler is tested
// by running this as a subprocess and reading what it leaves behind.
//
//   crash_probe <output-dir> [segv|abort|terminate|assert|none]
//
// The output directory stands in for the local data path, so a test can point
// it at a temporary directory. Nothing is watching to dismiss a dialog, so
// the probe silences both things that could raise one: RWE_CRASH_NO_DIALOG
// for the handler's own message box, and the debug CRT's report mode for the
// one the runtime would open before the handler ever runs.

#include <cassert>
#include <cstdlib>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#include <cstring>
#include <filesystem>
#include <iostream>
#include <rwe/util/CrashHandler.h>
#include <stdexcept>

namespace fs = std::filesystem;

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: crash_probe <output-dir> [segv|abort|terminate|assert|none]\n";
        return 2;
    }

#ifdef _WIN32
    _putenv_s("RWE_CRASH_NO_DIALOG", "1");
#else
    setenv("RWE_CRASH_NO_DIALOG", "1", 1);
#endif

    // A failed assert() in a Debug build does not reach the handler below.
    // It goes through _CrtDbgReport, whose default report mode is a modal
    // message box, and only raises SIGABRT afterwards -- so on a runner the
    // probe stops dead at a dialog nobody can close and the caller's
    // std::system waits for it for ever. The MSVC Debug job spent forty-five
    // minutes there before the timeout killed it.
    //
    // The other three modes get away with it by entering the runtime
    // somewhere else: segv, abort and terminate all reach the handler, which
    // _exit()s before anything can be drawn.
    //
    // rwe_test solves this for itself in src/rwe/test_crt_silence.cpp. The
    // probe is a separate binary and carries its own copy rather than
    // borrowing a file whose name says it belongs to the tests.
#if defined(_MSC_VER) && defined(_DEBUG)
    for (auto report : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
    {
        _CrtSetReportMode(report, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
    }

    // abort() otherwise raises its own dialog on the way out.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    fs::create_directories(argv[1]);

    rwe::setCrashScene("CrashProbeScene");
    rwe::setCrashMap("Probe Map");
    rwe::setCrashPhase(rwe::CrashPhase::SimTick);
    rwe::setCrashTick(1234, 5678, 42, 2);

    rwe::installCrashHandler(fs::path(argv[1]));

    // Tell the caller where to look before dying, so the test does not have to
    // reimplement the naming rule.
    std::cout << rwe::crashFilePath() << std::endl;
    std::cout.flush();

    auto mode = argc >= 3 ? argv[2] : "segv";

    if (std::strcmp(mode, "none") == 0)
    {
        // Installs the handler and exits normally, so a test can require that
        // the quiet path leaves no crash file behind.
        return 0;
    }

    if (std::strcmp(mode, "assert") == 0)
    {
        // A no-op under NDEBUG, so the test that drives this is compiled out
        // of a Release build too.
        assert(mode == nullptr && "crash_probe deliberate assertion");
        return 0;
    }

    if (std::strcmp(mode, "abort") == 0)
    {
        std::abort();
    }

    if (std::strcmp(mode, "terminate") == 0)
    {
        // No matching catch anywhere: this reaches std::terminate.
        throw std::runtime_error("crash_probe terminate");
    }

    // Through a volatile pointer so the null dereference survives the
    // optimiser rather than becoming an unreachable trap.
    volatile int* p = nullptr;
    *p = 1;

    return 0;
}
