#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <rwe/LoadingScene_util.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/util/SimpleLogger.h>
#include <string>

namespace rwe
{
    namespace
    {
        UnitFbi parseInfo(const std::string& keys)
        {
            auto tdf = parseTdfFromString(
                "[UNITINFO]\n{\nUnitName=ARMTSHIP;\nObjectname=ARMTSHIP;\nSoundCategory=NOSOUND;\n" + keys + "\n}\n");
            return parseUnitFbi(tdf);
        }

        UnitDefinition definitionOf(const std::string& keys)
        {
            MovementClassDatabase movementClasses;
            return parseUnitDefinition(parseInfo(keys), movementClasses);
        }

        std::string readLogFile(const std::filesystem::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
    }

    TEST_CASE("a transport's capacity comes out of the FBI", "[fbi]")
    {
        SECTION("TransportCapacity is the key the v3.1 data names")
        {
            auto d = definitionOf("TransportCapacity=20;\nTransportSize=3;\nCanLoad=1;");
            REQUIRE(d.transportCapacity == 20u);
            REQUIRE(d.effectiveTransportCapacity() == 20u);
        }

        SECTION("TransportMaxUnits is read when TransportCapacity is absent")
        {
            // totala1.hpi's unpatched ARMTSHIP.FBI, lowercase on purpose: the
            // TDF reader matches keys case-insensitively, so a fallback spelled
            // this way reaches the same place.
            auto d = definitionOf("transportmaxunits=20;\nTransportSize=3;\nCanLoad=1;");
            REQUIRE(d.transportCapacity == 20u);
            REQUIRE(d.effectiveTransportCapacity() == 20u);
        }

        SECTION("TransportCapacity wins when both keys are present")
        {
            // What a patched install looks like if a mod leaves the old key
            // behind: rev31.gp3's value is the one that counts.
            auto d = definitionOf("TransportCapacity=20;\nTransportMaxUnits=24;\nTransportSize=3;\nCanLoad=1;");
            REQUIRE(d.transportCapacity == 20u);
        }

        SECTION("a transport with neither key still carries one unit")
        {
            auto d = definitionOf("TransportSize=3;\nCanLoad=1;");
            REQUIRE(d.transportCapacity == 1u);
            REQUIRE(d.effectiveTransportCapacity() == 1u);
        }
    }

    TEST_CASE("a transport with no capacity key warns at load", "[fbi]")
    {
        auto logPath = std::filesystem::temp_directory_path() / "rwe-fbi-io-test.log";
        std::filesystem::remove(logPath);

        auto previous = globalLogger();
        setGlobalLogger(std::make_shared<SimpleLogger>(logPath.string(), true));

        parseInfo("transportmaxunits=20;\nTransportSize=3;\nCanLoad=1;");

        setGlobalLogger(previous);

        auto text = readLogFile(logPath);
        REQUIRE(text.find("ARMTSHIP") != std::string::npos);
        REQUIRE(text.find("TransportMaxUnits=20") != std::string::npos);
        REQUIRE(text.find("pre-3.1") != std::string::npos);

        std::filesystem::remove(logPath);
    }
}
