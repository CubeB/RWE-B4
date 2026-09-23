#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <rwe/game/SimEventLog.h>

namespace rwe
{
    namespace
    {
        std::vector<std::string> writeAndReadBack(const SimEventLog& log)
        {
            static int counter = 0;
            auto path = std::filesystem::temp_directory_path()
                / ("rwe-simeventlog-test-" + std::to_string(++counter) + ".jsonl");
            log.write(path);

            std::vector<std::string> lines;
            {
                // Scoped, so the stream is shut before the file is removed:
                // Windows refuses to delete a file anything still has open.
                std::ifstream in(path);
                std::string line;
                while (std::getline(in, line))
                {
                    lines.push_back(line);
                }
            }

            std::filesystem::remove(path);
            return lines;
        }
    }

    TEST_CASE("SimEventLog")
    {
        SimEventLog log;

        SECTION("keeps nothing at all until it is asked to record")
        {
            // The events are free to write and never free to keep: only an
            // arena run writes this log out, and an ordinary game that
            // recorded anyway would fill memory for the length of the match
            // with something nobody will read.
            log.event(10, "ai_status").set("metal", 100).detail("ticking over");
            REQUIRE(log.empty());
        }

        SECTION("keeps what is recorded once it is recording")
        {
            log.setRecording(true);
            log.event(10, "ai_status").set("metal", 100);
            REQUIRE_FALSE(log.empty());
        }

        SECTION("stops again when recording is turned off")
        {
            log.setRecording(true);
            log.event(10, "ai_status");
            log.setRecording(false);
            log.event(20, "ai_status");

            auto lines = writeAndReadBack(log);
            REQUIRE(lines.size() == 1);
            REQUIRE(lines[0].find("\"tick\":10") != std::string::npos);
        }

        SECTION("writes one line per event, with its tick and its seconds")
        {
            log.setRecording(true);
            log.event(30, "ai_status").set("metal", 100);
            log.event(60, "ai_transition").set("why", "tier_two");

            auto lines = writeAndReadBack(log);
            REQUIRE(lines.size() == 2);
            REQUIRE(lines[0].find("\"ev\":\"ai_status\"") != std::string::npos);
            REQUIRE(lines[0].find("\"metal\":100") != std::string::npos);
            REQUIRE(lines[1].find("\"why\":\"tier_two\"") != std::string::npos);

            // Sim ticks and seconds derived from them, never the wall clock:
            // that is what lets two same-seed runs be diffed line for line.
            REQUIRE(lines[1].find("\"tick\":60") != std::string::npos);
            REQUIRE(lines[1].find("\"secs\":2.0") != std::string::npos);
        }

        SECTION("an event built while not recording discards its fields safely")
        {
            // The call sites do not ask first, so the Event handed back has to
            // swallow everything set on it rather than index into nothing.
            auto event = log.event(10, "ai_status");
            event.set("a", 1).set("b", true).set("c", "three").detail("nothing");
            REQUIRE(log.empty());
        }

        SECTION("clear empties it")
        {
            log.setRecording(true);
            log.event(10, "ai_status");
            log.clear();
            REQUIRE(log.empty());
        }
    }
}
