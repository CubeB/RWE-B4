/**
 * Stop the MSVC debug runtime opening a dialog nobody can close.
 *
 * In a Debug build the CRT reports a failed assertion -- an out-of-range
 * subscript, an invalidated iterator, one of the `_ITERATOR_DEBUG_LEVEL`
 * checks -- by opening a modal message box. On a developer's machine that is
 * useful. On a CI runner there is nobody to click it, so the job stops dead
 * and keeps its slot until something kills it: `test rwe` sat there for two
 * hours and eighteen minutes on 2026-09-06 and had to be cancelled by hand,
 * having printed nothing about what it was upset by.
 *
 * Routing the report to stderr instead makes the same failure arrive as a
 * message and an abort, which is what a test run is supposed to do with one.
 *
 * This is a Debug-only, MSVC-only concern; every other toolchain compiles the
 * file to nothing.
 */

#if defined(_MSC_VER) && defined(_DEBUG)

#include <crtdbg.h>
#include <cstdlib>

namespace
{
    struct CrtReportToStderr
    {
        CrtReportToStderr()
        {
            for (auto report : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
            {
                _CrtSetReportMode(report, _CRTDBG_MODE_FILE);
                _CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
            }

            // abort() otherwise raises its own dialog on the way out.
            _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        }
    };

    // Runs before main, and so before any test can trip an assertion.
    const CrtReportToStderr installed;
}

#endif
