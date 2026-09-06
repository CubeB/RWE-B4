/**
 * Say which test is running, before it runs.
 *
 * A test that aborts -- a failed assertion, a checked-iterator violation, a
 * segfault -- takes the process with it, and Catch2's own per-case output is
 * printed when a case *finishes*. So the last thing in the log is the case
 * before the one that died, and the one you want is never named.
 *
 * That cost a day. The MSVC Debug run tripped `cannot increment
 * value-initialized deque iterator` on 2026-09-06 and the log could only say
 * which case had completed last; an attempt to work out what came next from
 * `--list-tests` failed because the listing order is not the run order (the
 * run started at listing entry 78 and died at 149).
 *
 * Off unless RWE_TEST_ANNOUNCE is set, because 450 extra lines are noise in a
 * local run and exactly what is wanted in a CI log. Written to stderr and
 * flushed a line at a time, so it survives the abort that made it necessary.
 */

#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <cstdio>
#include <cstdlib>

namespace
{
    class AnnounceTestCase : public Catch::EventListenerBase
    {
    public:
        using Catch::EventListenerBase::EventListenerBase;

        void testCaseStarting(const Catch::TestCaseInfo& info) override
        {
            if (std::getenv("RWE_TEST_ANNOUNCE") == nullptr)
            {
                return;
            }

            std::fputs("[running] ", stderr);
            std::fputs(info.name.c_str(), stderr);
            std::fputc('\n', stderr);
            std::fflush(stderr);
        }
    };
}

CATCH_REGISTER_LISTENER(AnnounceTestCase)
