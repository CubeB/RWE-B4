#include <catch2/catch_test_macros.hpp>

#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/util/Index.h>

namespace rwe
{
    TEST_CASE("parseOta")
    {
        SECTION("parses ota files")
        {

            std::string input = R"TDF(
[GlobalHeader]
    {
    missionname=Painted Desert;
    missiondescription=18 X 18  Battle in the Many Mesas area.;
    planet=Desert;
    missionhint=;
    brief=;
    narration=;
    glamour=;
    lineofsight=0;
    mapping=0;
    tidalstrength=0;
    solarstrength=35;
    lavaworld=0;
    killmul=50;
    timemul=0;
    minwindspeed=100;
    maxwindspeed=4000;
    gravity=112;
    numplayers=2, 3, 4, 6;
    size=19 x 19;
    memory=32 mb;
    useonlyunits=Painted Desert.tdf;
    SCHEMACOUNT=2;
    [Schema 0]
        {
        Type=Network 2;
        aiprofile=RADAI;
        SurfaceMetal=3;
        MohoMetal=40;
        HumanMetal=1000;
        ComputerMetal=2000;
        HumanEnergy=3000;
        ComputerEnergy=4000;
        MeteorWeapon=radMETEOR;
        MeteorRadius=1;
        MeteorDensity=.2;
        MeteorDuration=3;
        MeteorInterval=4;
        [specials]
            {
            [special0]
                {
                specialwhat=StartPos1;
                XPos=1824;
                ZPos=1008;
                }
            [special1]
                {
                specialwhat=StartPos2;
                XPos=7872;
                ZPos=8432;
                }
            }
        }
    [Schema 1]
        {
        Type=Network 3;
        aiprofile=;
        SurfaceMetal=5;
        MohoMetal=60;
        HumanMetal=1000;
        ComputerMetal=1000;
        HumanEnergy=1000;
        ComputerEnergy=1000;
        MeteorWeapon=;
        MeteorRadius=0;
        MeteorDensity=0;
        MeteorDuration=0;
        MeteorInterval=0;
        [features]
            {
            [feature0]
                {
                Featurename=DryMetal03;
                XPos=121;
                ZPos=495;
                }
            [feature1]
                {
                Featurename=DryMetal01;
                XPos=482;
                ZPos=455;
                }
            [feature2]
                {
                Featurename=DryMetal02;
                XPos=460;
                ZPos=111;
                }
            [feature3]
                {
                Featurename=DryMetal03;
                XPos=119;
                ZPos=108;
                }
            }
        [specials]
            {
            [special0]
                {
                specialwhat=StartPos1;
                XPos=1776;
                ZPos=1536;
                }
            [special1]
                {
                specialwhat=StartPos2;
                XPos=7488;
                ZPos=2016;
                }
            [special2]
                {
                specialwhat=StartPos3;
                XPos=7632;
                ZPos=7408;
                }
            [special3]
                {
                specialwhat=StartPos4;
                XPos=1856;
                ZPos=8096;
                }
            }
        }
    }
)TDF";

            OtaSchema schema0;
            schema0.type = "Network 2";
            schema0.aiProfile = "RADAI";
            schema0.surfaceMetal = 3;
            schema0.mohoMetal = 40;
            schema0.humanMetal = 1000;
            schema0.computerMetal = 2000;
            schema0.humanEnergy = 3000;
            schema0.computerEnergy = 4000;
            schema0.meteorWeapon = "radMETEOR";
            schema0.meteorRadius = 1;
            schema0.meteorDensity = 0.2f;
            schema0.meteorDuration = 3;
            schema0.meteorInterval = 4;
            schema0.specials = std::vector<OtaSpecial>{
                {"StartPos1", 1824, 1008},
                {"StartPos2", 7872, 8432},
            };
            schema0.features = std::vector<OtaFeature>();

            OtaSchema schema1;
            schema1.type = "Network 3";
            schema1.aiProfile = "";
            schema1.surfaceMetal = 5;
            schema1.mohoMetal = 60;
            schema1.humanMetal = 1000;
            schema1.computerMetal = 1000;
            schema1.humanEnergy = 1000;
            schema1.computerEnergy = 1000;
            schema1.meteorWeapon = "";
            schema1.meteorRadius = 0;
            schema1.meteorDensity = 0;
            schema1.meteorDuration = 0;
            schema1.meteorInterval = 0;
            schema1.features = std::vector<OtaFeature>{
                {"DryMetal03", 121, 495},
                {"DryMetal01", 482, 455},
                {"DryMetal02", 460, 111},
                {"DryMetal03", 119, 108},
            };
            schema1.specials = std::vector<OtaSpecial>{
                {"StartPos1", 1776, 1536},
                {"StartPos2", 7488, 2016},
                {"StartPos3", 7632, 7408},
                {"StartPos4", 1856, 8096},
            };

            OtaRecord ota;
            ota.missionName = "Painted Desert";
            ota.missionDescription = "18 X 18  Battle in the Many Mesas area.";
            ota.planet = "Desert";
            ota.missionHint = "";
            ota.brief = "";
            ota.narration = "";
            ota.glamour = "";
            ota.lineOfSight = 0;
            ota.mapping = 0;
            ota.tidalStrength = 0;
            ota.solarStrength = 35;
            ota.lavaWorld = false;
            ota.killMul = 50;
            ota.timeMul = 0;
            ota.minWindSpeed = 100;
            ota.maxWindSpeed = 4000;
            ota.gravity = 112;
            ota.numPlayers = "2, 3, 4, 6";
            ota.size = "19 x 19";
            ota.memory = "32 mb";
            ota.useOnlyUnits = "Painted Desert.tdf";
            ota.schemaCount = 2;
            ota.schemas.push_back(schema0);
            ota.schemas.push_back(schema1);

            auto parsedRecord = parseOta(parseTdfFromString(input));


            REQUIRE(parsedRecord.missionName == ota.missionName);
            REQUIRE(parsedRecord.missionDescription == ota.missionDescription);
            REQUIRE(parsedRecord.planet == ota.planet);
            REQUIRE(parsedRecord.missionHint == ota.missionHint);
            REQUIRE(parsedRecord.brief == ota.brief);
            REQUIRE(parsedRecord.narration == ota.narration);
            REQUIRE(parsedRecord.glamour == ota.glamour);
            REQUIRE(parsedRecord.lineOfSight == ota.lineOfSight);
            REQUIRE(parsedRecord.mapping == ota.mapping);
            REQUIRE(parsedRecord.tidalStrength == ota.tidalStrength);
            REQUIRE(parsedRecord.solarStrength == ota.solarStrength);
            REQUIRE(parsedRecord.lavaWorld == ota.lavaWorld);
            REQUIRE(parsedRecord.killMul == ota.killMul);
            REQUIRE(parsedRecord.timeMul == ota.timeMul);
            REQUIRE(parsedRecord.minWindSpeed == ota.minWindSpeed);
            REQUIRE(parsedRecord.maxWindSpeed == ota.maxWindSpeed);
            REQUIRE(parsedRecord.gravity == ota.gravity);
            REQUIRE(parsedRecord.numPlayers == ota.numPlayers);
            REQUIRE(parsedRecord.size == ota.size);
            REQUIRE(parsedRecord.memory == ota.memory);
            REQUIRE(parsedRecord.useOnlyUnits == ota.useOnlyUnits);
            REQUIRE(parsedRecord.schemaCount == ota.schemaCount);

            REQUIRE(parsedRecord.schemas.size() == ota.schemas.size());

            for (Index i = 0; i < getSize(parsedRecord.schemas); ++i)
            {
                const auto& a = parsedRecord.schemas[i];
                const auto& b = ota.schemas[i];

                REQUIRE(a.type == b.type);
                REQUIRE(a.aiProfile == b.aiProfile);
                REQUIRE(a.surfaceMetal == b.surfaceMetal);
                REQUIRE(a.mohoMetal == b.mohoMetal);
                REQUIRE(a.humanMetal == b.humanMetal);
                REQUIRE(a.computerMetal == b.computerMetal);
                REQUIRE(a.humanEnergy == b.humanEnergy);
                REQUIRE(a.computerEnergy == b.computerEnergy);
                REQUIRE(a.meteorWeapon == b.meteorWeapon);
                REQUIRE(a.meteorRadius == b.meteorRadius);
                REQUIRE(a.meteorDensity == b.meteorDensity);
                REQUIRE(a.meteorDuration == b.meteorDuration);
                REQUIRE(a.meteorInterval == b.meteorInterval);

                REQUIRE(a.specials.size() == b.specials.size());
                for (Index j = 0; j < getSize(a.specials); ++j)
                {
                    const auto& sa = a.specials[j];
                    const auto& sb = b.specials[j];

                    REQUIRE(sa.specialWhat == sb.specialWhat);
                    REQUIRE(sa.xPos == sb.xPos);
                    REQUIRE(sa.zPos == sb.zPos);
                }

                REQUIRE(a.features.size() == b.features.size());
                for (Index j = 0; j < getSize(a.features); ++j)
                {
                    const auto& fa = a.features[j];
                    const auto& fb = b.features[j];

                    REQUIRE(fa.featureName == fb.featureName);
                    REQUIRE(fa.xPos == fb.xPos);
                    REQUIRE(fa.zPos == fb.zPos);
                }
            }
        }

        SECTION("assumes defaults for missing schema values")
        {

            std::string input = R"TDF(
[GlobalHeader]
    {
    missionname=Painted Desert;
    SCHEMACOUNT=1;
    [Schema 0]
        {
        Type=Network 1;
        }
    }
)TDF";

            OtaSchema schema0;
            schema0.type = "Network 1";
            schema0.aiProfile = "DEFAULT";
            schema0.surfaceMetal = 3;
            schema0.mohoMetal = 30;
            schema0.humanMetal = 1000;
            schema0.computerMetal = 1000;
            schema0.humanEnergy = 1000;
            schema0.computerEnergy = 1000;
            schema0.meteorWeapon = "";
            schema0.meteorRadius = 0;
            schema0.meteorDensity = 0.0f;
            schema0.meteorDuration = 0;
            schema0.meteorInterval = 0;

            OtaRecord ota;
            ota.missionName = "Painted Desert";
            ota.missionDescription = "";
            ota.planet = "";
            ota.missionHint = "";
            ota.brief = "";
            ota.narration = "";
            ota.glamour = "";
            ota.lineOfSight = 0;
            ota.mapping = 0;
            ota.tidalStrength = 0;
            ota.solarStrength = 0;
            ota.lavaWorld = false;
            ota.killMul = 50;
            ota.timeMul = 0;
            ota.minWindSpeed = 0;
            ota.maxWindSpeed = 0;
            ota.gravity = 112;
            ota.numPlayers = "";
            ota.size = "";
            ota.memory = "";
            ota.useOnlyUnits = "";
            ota.schemaCount = 1;
            ota.schemas.push_back(schema0);

            auto parsedRecord = parseOta(parseTdfFromString(input));

            REQUIRE(parsedRecord.missionName == ota.missionName);
            REQUIRE(parsedRecord.missionDescription == ota.missionDescription);
            REQUIRE(parsedRecord.planet == ota.planet);
            REQUIRE(parsedRecord.missionHint == ota.missionHint);
            REQUIRE(parsedRecord.brief == ota.brief);
            REQUIRE(parsedRecord.narration == ota.narration);
            REQUIRE(parsedRecord.glamour == ota.glamour);
            REQUIRE(parsedRecord.lineOfSight == ota.lineOfSight);
            REQUIRE(parsedRecord.mapping == ota.mapping);
            REQUIRE(parsedRecord.tidalStrength == ota.tidalStrength);
            REQUIRE(parsedRecord.solarStrength == ota.solarStrength);
            REQUIRE(parsedRecord.lavaWorld == ota.lavaWorld);
            REQUIRE(parsedRecord.killMul == ota.killMul);
            REQUIRE(parsedRecord.timeMul == ota.timeMul);
            REQUIRE(parsedRecord.minWindSpeed == ota.minWindSpeed);
            REQUIRE(parsedRecord.maxWindSpeed == ota.maxWindSpeed);
            REQUIRE(parsedRecord.gravity == ota.gravity);
            REQUIRE(parsedRecord.numPlayers == ota.numPlayers);
            REQUIRE(parsedRecord.size == ota.size);
            REQUIRE(parsedRecord.memory == ota.memory);
            REQUIRE(parsedRecord.useOnlyUnits == ota.useOnlyUnits);
            REQUIRE(parsedRecord.schemaCount == ota.schemaCount);

            REQUIRE(parsedRecord.schemas.size() == ota.schemas.size());

            for (Index i = 0; i < getSize(parsedRecord.schemas); ++i)
            {
                const auto& a = parsedRecord.schemas[i];
                const auto& b = ota.schemas[i];

                REQUIRE(a.type == b.type);
                REQUIRE(a.aiProfile == b.aiProfile);
                REQUIRE(a.surfaceMetal == b.surfaceMetal);
                REQUIRE(a.mohoMetal == b.mohoMetal);
                REQUIRE(a.humanMetal == b.humanMetal);
                REQUIRE(a.computerMetal == b.computerMetal);
                REQUIRE(a.humanEnergy == b.humanEnergy);
                REQUIRE(a.computerEnergy == b.computerEnergy);
                REQUIRE(a.meteorWeapon == b.meteorWeapon);
                REQUIRE(a.meteorRadius == b.meteorRadius);
                REQUIRE(a.meteorDensity == b.meteorDensity);
                REQUIRE(a.meteorDuration == b.meteorDuration);
                REQUIRE(a.meteorInterval == b.meteorInterval);

                REQUIRE(a.specials.size() == b.specials.size());
                for (Index j = 0; j < getSize(a.specials); ++j)
                {
                    const auto& sa = a.specials[j];
                    const auto& sb = b.specials[j];

                    REQUIRE(sa.specialWhat == sb.specialWhat);
                    REQUIRE(sa.xPos == sb.xPos);
                    REQUIRE(sa.zPos == sb.zPos);
                }

                REQUIRE(a.features.size() == b.features.size());
                for (Index j = 0; j < getSize(a.features); ++j)
                {
                    const auto& fa = a.features[j];
                    const auto& fb = b.features[j];

                    REQUIRE(fa.featureName == fb.featureName);
                    REQUIRE(fa.xPos == fb.xPos);
                    REQUIRE(fa.zPos == fb.zPos);
                }
            }
        }
    }

    TEST_CASE("findStartPosition: a slot has a seat only where the map declares one")
    {
        // Three positions with a gap at 3, the way a hand-edited map or a
        // campaign schema can come: slots 1, 2 and 4 seat, 3 and 5 do not.
        std::string input = R"TDF(
[GlobalHeader]
    {
    missionname=Gappy;
    SCHEMACOUNT=1;
    [Schema 0]
        {
        Type=Network 3;
        [specials]
            {
            [special0]
                {
                specialwhat=StartPos1;
                XPos=100;
                ZPos=200;
                }
            [special1]
                {
                specialwhat=StartPos2;
                XPos=300;
                ZPos=400;
                }
            [special2]
                {
                specialwhat=StartPos4;
                XPos=500;
                ZPos=600;
                }
            [special3]
                {
                specialwhat=Lightning;
                XPos=1;
                ZPos=2;
                }
            }
        }
    }
)TDF";
        auto ota = parseOta(parseTdfFromString(input));
        REQUIRE(ota.schemas.size() == 1);
        const auto& schema = ota.schemas[0];

        SECTION("declared positions come back with their coordinates")
        {
            auto first = findStartPosition(schema, 1);
            REQUIRE(first);
            REQUIRE(first->xPos == 100);
            REQUIRE(first->zPos == 200);

            auto fourth = findStartPosition(schema, 4);
            REQUIRE(fourth);
            REQUIRE(fourth->xPos == 500);
            REQUIRE(fourth->zPos == 600);
        }

        SECTION("a gap and the far end come back empty rather than throwing")
        {
            REQUIRE(!findStartPosition(schema, 3));
            REQUIRE(!findStartPosition(schema, 5));
            REQUIRE(!findStartPosition(schema, 10));
        }

        SECTION("the count skips the gap and ignores other specials")
        {
            REQUIRE(countStartPositions(schema) == 3);
        }

        SECTION("a schema with no specials at all seats nobody")
        {
            OtaSchema empty;
            REQUIRE(!findStartPosition(empty, 1));
            REQUIRE(countStartPositions(empty) == 0);
        }
    }

    TEST_CASE("a mission's header keys, conditions and starting units are read", "[ota][campaign]")
    {
        // Cut down from totala4.hpi Maps/AC01.OTA, the first Arm mission, as
        // shipped; nomovie and the water keys added to exercise them.
        auto tdf = parseTdfFromString(R"(
[GlobalHeader]
	{
	missionname=1: A Hero Returns;
	missiondescription=;
	planet=Green planet;
	missionhint=Ac01hint0.txt;
	brief=ArmCampaign1;
	narration=arm01;
	glamour=arm01;
	useonlyunits=AC01.tdf;
	nomovie=1;
	waterdoesdamage=0;
	waterdamage=100;
	MoveUnitToRadius=ANYTYPE, 992, 656, 64;
	AllUnitsKilled=1;
	AllUnitsKilledOfType=ARMGATE;
	SCHEMACOUNT=1;
	[Schema 0]
		{
		Type=Easy;
		aiprofile=MISSIONS;
		[units]
			{
			[unit0]
				{
				Unitname=ARMFAV;
				Ident=;
				XPos=1099;
				YPos=85;
				ZPos=2402;
				Player=1;
				HealthPercentage=100;
				Angle=0;
				Kills=0;
				}
			[unit1]
				{
				Unitname=CORAK;
				Ident=;
				XPos=372;
				YPos=85;
				ZPos=1813;
				Player=2;
				HealthPercentage=100;
				Angle=0;
				Kills=0;
				InitialMission=p 1750 1828;
				}
			}
		}
	}
)");
        auto ota = parseOta(tdf);

        REQUIRE(ota.maxUnits == 200);
        REQUIRE(ota.noMovie);
        REQUIRE_FALSE(ota.waterDoesDamage);
        REQUIRE(ota.waterDamage == 100);
        REQUIRE(ota.useOnlyUnits == "AC01.tdf");

        // MoveUnitToRadius is `%[a-zA-Z],%i,%i,%i`; the spaces after the
        // commas are what %i skips.
        REQUIRE(ota.rules.moveUnitToRadius.has_value());
        REQUIRE(ota.rules.moveUnitToRadius->unitType == "ANYTYPE");
        REQUIRE(ota.rules.moveUnitToRadius->x == 992);
        REQUIRE(ota.rules.moveUnitToRadius->z == 656);
        REQUIRE(ota.rules.moveUnitToRadius->radius == 64);
        REQUIRE(ota.rules.allUnitsKilled == 1);
        REQUIRE(ota.rules.allUnitsKilledOfType == std::optional<std::string>("ARMGATE"));
        REQUIRE(ota.rules.commanderKilled == 0);
        REQUIRE_FALSE(ota.rules.killUnitType.has_value());
        // Absent, these two are -1 rather than 0, which is a real line.
        REQUIRE(ota.rules.anyUnitPassesX == -1);
        REQUIRE(ota.rules.anyUnitPassesZ == -1);

        const auto& units = ota.schemas.at(0).units;
        REQUIRE(units.size() == 2);
        REQUIRE(units[0].unitName == "ARMFAV");
        REQUIRE(units[0].xPos == 1099);
        REQUIRE(units[0].yPos == 85);
        REQUIRE(units[0].zPos == 2402);
        REQUIRE(units[0].player == 1);
        REQUIRE(units[0].healthPercentage == 100);
        REQUIRE(units[0].orders.empty());

        REQUIRE(units[1].player == 2);
        REQUIRE(units[1].orders.size() == 1);
        REQUIRE(units[1].orders[0].kind == MissionOrder::Kind::Patrol);
        REQUIRE(units[1].orders[0].numbers == std::vector<float>{1750.0f, 1828.0f});
    }

    TEST_CASE("AnyUnitPassesZ=0 is a line at the map's top edge, not a missing key", "[ota][campaign]")
    {
        // CC19's is AnyUnitPassesZ=60, a few cells from the top; 0x48E92C
        // builds the rule for anything from 0 up.
        auto tdf = parseTdfFromString("[GlobalHeader]\n{\nAnyUnitPassesZ=0;\nAnyUnitPassesX=60;\n}\n");
        auto rules = parseOtaMissionRules(tdf.findBlock("GlobalHeader")->get());
        REQUIRE(rules.anyUnitPassesZ == 0);
        REQUIRE(rules.anyUnitPassesX == 60);
    }

    TEST_CASE("InitialMission is read the way 0x487BF0 reads it", "[ota][campaign]")
    {
        using K = MissionOrder::Kind;

        SECTION("the shapes the shipped missions use")
        {
            auto waitThenPatrol = parseInitialMission("w 30,p 1500 900,");
            REQUIRE(waitThenPatrol.size() == 2);
            REQUIRE(waitThenPatrol[0].kind == K::Wait);
            REQUIRE(waitThenPatrol[0].numbers == std::vector<float>{30.0f});
            REQUIRE(waitThenPatrol[1].kind == K::Patrol);

            auto huntCommander = parseInitialMission("w 10,a CORCOM,");
            REQUIRE(huntCommander[1].kind == K::AttackType);
            REQUIRE(huntCommander[1].name == "CORCOM");

            auto standing = parseInitialMission("o 0 1,w 5,");
            REQUIRE(standing[0].kind == K::StandingOrders);
            REQUIRE(standing[0].numbers == std::vector<float>{0.0f, 1.0f});

            REQUIRE(parseInitialMission("wa")[0].kind == K::WaitForAttack);
            REQUIRE(parseInitialMission("i CHRIS,")[0].name == "CHRIS");
        }

        SECTION("the letter is either case")
        {
            auto upper = parseInitialMission("P 502 1223");
            REQUIRE(upper[0].kind == K::Patrol);
            REQUIRE(upper[0].numbers == std::vector<float>{502.0f, 1223.0f});
        }

        SECTION("a typo in the shipped data scans nothing, as sscanf would")
        {
            // AC01 carries `InitialMission=P P 502 1224;` three times. The
            // arguments are scanned from just after the letter, and the
            // second P stops the first %f.
            auto typo = parseInitialMission("P P 502 1224");
            REQUIRE(typo.size() == 1);
            REQUIRE(typo[0].kind == K::Patrol);
            REQUIRE(typo[0].numbers.empty());
        }

        SECTION("a point attack, a build, and the orders with no arguments")
        {
            auto orders = parseInitialMission("a 100 200,b ARMLLT 1 300 400,d,s,z");
            REQUIRE(orders.size() == 5);
            REQUIRE(orders[0].kind == K::AttackPoint);
            REQUIRE(orders[0].numbers == std::vector<float>{100.0f, 200.0f});
            REQUIRE(orders[1].kind == K::Build);
            REQUIRE(orders[1].name == "ARMLLT");
            REQUIRE(orders[1].numbers == std::vector<float>{1.0f, 300.0f, 400.0f});
            REQUIRE(orders[2].kind == K::SelfDestruct);
            REQUIRE(orders[3].kind == K::MakeSelectable);
            // A letter the table does not know is skipped: 0x487E50 is only
            // the loop going round.
            REQUIRE(orders[4].kind == K::Skip);
        }

        SECTION("a silo's stockpile, and an attack with one number is a name")
        {
            auto orders = parseInitialMission("bw 10,a 100,");
            REQUIRE(orders.size() == 2);
            REQUIRE(orders[0].kind == K::BuildWeapon);
            REQUIRE(orders[0].numbers == std::vector<float>{10.0f});
            // The point form wants both numbers (0x487F4F); otherwise the
            // same text is read again as a type name.
            REQUIRE(orders[1].kind == K::AttackType);
            REQUIRE(orders[1].name == "100");
        }
    }

    TEST_CASE("a unit's flags and its group come out of their keys", "[ota][campaign]")
    {
        auto tdf = parseTdfFromString(R"(
[unit0]
	{
	Unitname=ARMCOM;
	Ident=HERO;
	InitialGroup=3;
	MissionCriticalUnit=1;
	AiPriorityTarget=1;
	Immunity=1;
	CreationCountdown=90;
	}
)");
        auto u = parseOtaMissionUnit(tdf.findBlock("unit0")->get());
        REQUIRE(u.ident == "HERO");
        REQUIRE(u.initialGroup == 3);
        REQUIRE(u.missionCriticalUnit);
        REQUIRE_FALSE(u.aiIgnore);
        REQUIRE(u.aiPriorityTarget);
        REQUIRE(u.immunity);
        REQUIRE(u.creationCountdown == 90);
        // HealthPercentage defaults to 100.
        REQUIRE(u.healthPercentage == 100);
    }
}
