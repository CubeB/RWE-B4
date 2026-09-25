#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitDefinition.h>
#include <string>

/**
 * Issue #199. A transport's capacity is written two ways in the shipped
 * data: totala1.hpi's ARMTSHIP.FBI says transportmaxunits=20, the 1.0 key,
 * and rev31.gp3's says transportcapacity=20. The 3.1 exe reads only the
 * second (TOTALA-EXE-DATA.md §30), so on an install without the patch data
 * the original would leave the Hulk unable to load; RWE reads the old key
 * when the new one is absent and says so at load (TOTALA-EXE.md §88).
 *
 * The keys below are the two files' own, as far as the capacity rule reads
 * them: transportsize, Floater and whichever capacity key the file has.
 */
namespace rwe
{
    namespace
    {
        UnitFbi fbiFrom(const std::string& keys)
        {
            return parseUnitFbi(parseTdfFromString("[UNITINFO]\n{\nUnitName=ARMTSHIP;\nObjectname=model;\nSoundCategory=NOSOUND;\ncanload=1;\ntransportsize=3;\n" + keys + "\n}\n"));
        }

        unsigned int capacityOf(const UnitFbi& fbi)
        {
            MovementClassDatabase movementClasses;
            return parseUnitDefinition(fbi, movementClasses).transportCapacity;
        }
    }

    TEST_CASE("a transport's capacity is read from whichever key its FBI has", "[fbi][transport]")
    {
        SECTION("rev31.gp3: transportcapacity, as the 3.1 exe reads it, and nothing to say")
        {
            auto fbi = fbiFrom("transportcapacity=20;\nFloater=1;");
            REQUIRE(capacityOf(fbi) == 20);
            REQUIRE_FALSE(transportCapacityWarning(fbi).has_value());
        }

        SECTION("totala1.hpi: only the 1.0 key, which is read, and a warning that names it")
        {
            auto fbi = fbiFrom("Floater=1;\ntransportmaxunits=20;");
            REQUIRE(fbi.transportCapacity == 0);
            REQUIRE(fbi.transportMaxUnits == 20);
            REQUIRE(capacityOf(fbi) == 20);
            auto warning = transportCapacityWarning(fbi);
            REQUIRE(warning.has_value());
            REQUIRE(warning->find("ARMTSHIP") != std::string::npos);
            REQUIRE(warning->find("TransportMaxUnits=20") != std::string::npos);
        }

        SECTION("both: the 3.1 key wins, as it does in the original")
        {
            // CORTSHIP's rev31 file says 5 where its 1.0 file said 24
            // (TOTALA-EXE-TRANSPORTS.md §32); a file carrying both reads 5.
            auto fbi = fbiFrom("transportcapacity=5;\nFloater=1;\ntransportmaxunits=24;");
            REQUIRE(capacityOf(fbi) == 5);
            REQUIRE_FALSE(transportCapacityWarning(fbi).has_value());
        }

        SECTION("neither: six at sea and one in the air, with a warning")
        {
            REQUIRE(capacityOf(fbiFrom("Floater=1;")) == 6);
            REQUIRE(capacityOf(fbiFrom("canfly=1;")) == 1);
            auto warning = transportCapacityWarning(fbiFrom("Floater=1;"));
            REQUIRE(warning.has_value());
            REQUIRE(warning->find("carrying 6") != std::string::npos);
        }
    }

    TEST_CASE("a unit that carries nothing has no capacity and no warning", "[fbi][transport]")
    {
        auto fbi = parseUnitFbi(parseTdfFromString("[UNITINFO]\n{\nUnitName=ARMPW;\nObjectname=model;\nSoundCategory=NOSOUND;\n}\n"));
        REQUIRE(capacityOf(fbi) == 0);
        REQUIRE_FALSE(transportCapacityWarning(fbi).has_value());
    }
}
