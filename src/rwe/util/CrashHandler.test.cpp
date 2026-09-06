#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <rwe/util/CrashHandler.h>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace rwe
{
    namespace
    {
        std::string readAll(const fs::path& path)
        {
            std::ifstream f(path, std::ios::binary);
            std::ostringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }

        /** The one crash file the probe should have left in dir, if any. */
        std::optional<fs::path> findCrashFile(const fs::path& dir)
        {
            if (!fs::exists(dir))
            {
                return std::nullopt;
            }
            for (const auto& entry : fs::directory_iterator(dir))
            {
                auto name = entry.path().filename().string();
                if (name.rfind("rwe-crash-", 0) == 0)
                {
                    return entry.path();
                }
            }
            return std::nullopt;
        }

        fs::path uniqueTempDir()
        {
            auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            return fs::temp_directory_path() / ("rwe-crash-test-" + std::to_string(stamp));
        }

        /** Runs the probe quietly: a deliberate crash is noisy on stderr. */
        int runProbe(const fs::path& dir, const std::string& mode)
        {
            std::string command = "\"" RWE_CRASH_PROBE_PATH "\" \"" + dir.string() + "\" " + mode;
#ifdef _WIN32
            command += " >nul 2>&1";
#else
            command += " >/dev/null 2>&1";
#endif
            return std::system(command.c_str());
        }
    }

    TEST_CASE("formatCrashReport", "[crash]")
    {
        CrashContext ctx;
        ctx.phase.store(static_cast<uint32_t>(CrashPhase::SimTick));
        ctx.sceneTime.store(1234);
        ctx.gameTime.store(5678);
        ctx.unitCount.store(42);
        ctx.playerCount.store(2);
        setCrashScene("GameScene");
        setCrashMap("Coast To Coast");
        // setCrashScene writes the global context, not ours; copy it across so
        // the assertions below are about formatting and nothing else.
        std::snprintf(ctx.scene, CrashContext::NameSize, "%s", "GameScene");
        std::snprintf(ctx.map, CrashContext::NameSize, "%s", "Coast To Coast");

        char buf[4096];

        SECTION("reports the fault, the address and what the game was doing")
        {
            auto length = formatCrashReport(buf, sizeof(buf), "SIGSEGV (segmentation fault)", reinterpret_cast<void*>(0xdeadbeef), ctx);

            std::string report(buf);
            REQUIRE(length == report.size());

            using Catch::Matchers::ContainsSubstring;
            REQUIRE_THAT(report, ContainsSubstring("RWE crash report"));
            REQUIRE_THAT(report, ContainsSubstring("SIGSEGV (segmentation fault)"));
            REQUIRE_THAT(report, ContainsSubstring("deadbeef"));
            REQUIRE_THAT(report, ContainsSubstring("SimTick"));
            REQUIRE_THAT(report, ContainsSubstring("GameScene"));
            REQUIRE_THAT(report, ContainsSubstring("Coast To Coast"));
            REQUIRE_THAT(report, ContainsSubstring("1234"));
            REQUIRE_THAT(report, ContainsSubstring("5678"));
            REQUIRE_THAT(report, ContainsSubstring("backtrace:"));
        }

        SECTION("a null address still formats")
        {
            formatCrashReport(buf, sizeof(buf), "SIGABRT (abort)", nullptr, ctx);
            using Catch::Matchers::ContainsSubstring;
            REQUIRE_THAT(std::string(buf), ContainsSubstring("0x0000000"));
        }

        SECTION("truncates into a small buffer instead of overrunning it")
        {
            // A canary either side: the formatter must touch neither.
            char guarded[64 + 2];
            guarded[0] = '\x7f';
            guarded[65] = '\x7f';

            auto length = formatCrashReport(guarded + 1, 64, "SIGSEGV", nullptr, ctx);

            REQUIRE(guarded[0] == '\x7f');
            REQUIRE(guarded[65] == '\x7f');
            REQUIRE(length < 64);
            REQUIRE(guarded[1 + length] == '\0');
        }

        SECTION("a zero-sized buffer writes nothing at all")
        {
            char guarded[1];
            guarded[0] = '\x7f';
            REQUIRE(formatCrashReport(guarded, 0, "SIGSEGV", nullptr, ctx) == 0);
            REQUIRE(guarded[0] == '\x7f');
        }
    }

    TEST_CASE("formatCrashFilePath", "[crash]")
    {
        // 2026-09-06 14:30:15 local time, built through mktime so the
        // expectation does not depend on the test machine's zone.
        std::tm when{};
        when.tm_year = 2026 - 1900;
        when.tm_mon = 8;
        when.tm_mday = 6;
        when.tm_hour = 14;
        when.tm_min = 30;
        when.tm_sec = 15;
        when.tm_isdst = -1;
        auto stamp = std::mktime(&when);

        char buf[256];

        SECTION("names the file after the time and puts it in the given directory")
        {
            REQUIRE(formatCrashFilePath(buf, sizeof(buf), "/home/someone/.rwe", stamp));
            REQUIRE(std::string(buf) == "/home/someone/.rwe/rwe-crash-20260906-143015.txt");
        }

        SECTION("does not double the separator when the directory ends in one")
        {
            REQUIRE(formatCrashFilePath(buf, sizeof(buf), "/home/someone/.rwe/", stamp));
            REQUIRE(std::string(buf) == "/home/someone/.rwe/rwe-crash-20260906-143015.txt");
        }

        SECTION("a Windows separator counts as one too")
        {
            REQUIRE(formatCrashFilePath(buf, sizeof(buf), "C:\\Users\\someone\\RWE\\", stamp));
            REQUIRE(std::string(buf) == "C:\\Users\\someone\\RWE\\rwe-crash-20260906-143015.txt");
        }

        SECTION("fails rather than truncate: a crash file under a wrong name is worse than none")
        {
            char small[16];
            REQUIRE_FALSE(formatCrashFilePath(small, sizeof(small), "/home/someone/.rwe", stamp));
        }
    }

    TEST_CASE("the crash handler entry latches", "[crash]")
    {
        resetCrashHandlerEntryForTesting();

        // A fault raised from inside the handler must not send it round again.
        REQUIRE(claimCrashHandlerEntry());
        REQUIRE_FALSE(claimCrashHandlerEntry());
        REQUIRE_FALSE(claimCrashHandlerEntry());

        resetCrashHandlerEntryForTesting();
        REQUIRE(claimCrashHandlerEntry());

        resetCrashHandlerEntryForTesting();
    }

    // Only a real fault exercises the signal handling, the alternate stack and
    // the platform backtrace, so this runs a process that genuinely crashes.
    TEST_CASE("a crashing process leaves a report behind", "[crash]")
    {
        auto dir = uniqueTempDir();
        fs::remove_all(dir);

        auto mode = GENERATE(std::string("segv"), std::string("abort"), std::string("terminate"));

        INFO("crash mode: " << mode);
        runProbe(dir, mode);

        auto crashFile = findCrashFile(dir);
        REQUIRE(crashFile.has_value());

        auto report = readAll(*crashFile);
        using Catch::Matchers::ContainsSubstring;

        REQUIRE_THAT(report, ContainsSubstring("RWE crash report"));
        REQUIRE_THAT(report, ContainsSubstring("backtrace:"));
        // The context the probe set before crashing has to survive into the
        // report, or the handler is reporting a fault with no story attached.
        REQUIRE_THAT(report, ContainsSubstring("CrashProbeScene"));
        REQUIRE_THAT(report, ContainsSubstring("Probe Map"));
        REQUIRE_THAT(report, ContainsSubstring("SimTick"));
        REQUIRE_THAT(report, ContainsSubstring("1234"));

        fs::remove_all(dir);
    }

    TEST_CASE("a clean exit leaves no crash file", "[crash]")
    {
        auto dir = uniqueTempDir();
        fs::remove_all(dir);

        // Installing the handler must not itself create the file: an ordinary
        // session that quits normally should leave nothing behind.
        REQUIRE(runProbe(dir, "none") == 0);
        REQUIRE(findCrashFile(dir) == std::nullopt);

        fs::remove_all(dir);
    }
}
