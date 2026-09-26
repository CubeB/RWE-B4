#include <catch2/catch_test_macros.hpp>
#include <rwe/io/campaign/campaign.h>
#include <rwe/io/tdf/tdf.h>

namespace rwe
{
    TEST_CASE("the campaign file lists its missions and their names", "[campaign]")
    {
        // The start of ccdata.ccx CAMPS/Arm Campaign.tdf, as shipped (the
        // accented translations left out).
        auto tdf = parseTdfFromString(R"(
[HEADER]
	{
	campaignside=ARM;
	}

[MISSION0]
	{
	missionfile=AC01.ota;
	missionname=1: A Hero Returns;
	Italianmissionname=1: Ritorno di un eroe;
	}

[MISSION1]
	{
	missionfile=AC02.ota;
	missionname=2: CORE KBot Base, Destroy It!;
	}

[MISSION3]
	{
	missionfile=AC04.ota;
	missionname=4: CORE Contamination Spreads...;
	}
)");
        auto campaign = parseCampaign(tdf);

        REQUIRE(campaign.side == "ARM");
        // MISSION%d from 0 until one is missing: MISSION3 is never reached.
        REQUIRE(campaign.missions.size() == 2);
        REQUIRE(campaign.missions[0].missionFile == "AC01.ota");
        REQUIRE(campaign.missions[1].missionFile == "AC02.ota");

        REQUIRE(campaignMissionName(campaign.missions[0], "") == "1: A Hero Returns");
        REQUIRE(campaignMissionName(campaign.missions[0], "Italian") == "1: Ritorno di un eroe");
        // No German name for this one: RWE falls back to the plain key.
        REQUIRE(campaignMissionName(campaign.missions[1], "German") == "2: CORE KBot Base, Destroy It!");
    }

    TEST_CASE("a campaign with no side is for both, and a nameless mission says so", "[campaign]")
    {
        auto tdf = parseTdfFromString(R"(
[HEADER]
	{
	}
[MISSION0]
	{
	missionfile=X.ota;
	}
)");
        auto campaign = parseCampaign(tdf);
        REQUIRE(campaign.side == "ALL");
        REQUIRE(campaignMissionName(campaign.missions[0], "") == "Error -- Unnamed Mission");
    }

    TEST_CASE("a mission is played on its missionfile without the extension", "[campaign]")
    {
        CampaignMission mission;
        mission.missionFile = "EXP1AC01.ota";
        REQUIRE(campaignMissionMapName(mission) == "EXP1AC01");
        mission.missionFile = "AC02.OTA";
        REQUIRE(campaignMissionMapName(mission) == "AC02");
        mission.missionFile = "Plain Map";
        REQUIRE(campaignMissionMapName(mission) == "Plain Map");
    }

    TEST_CASE("a mission between missions wears the thumb for how it went", "[campaign]")
    {
        // The list font's glyphs 0xFE, 0xFF and 0xFD, as UTF-8.
        REQUIRE(campaignMissionListEntry('W', "1: A Hero Returns") == "\xC3\xBE 1: A Hero Returns");
        REQUIRE(campaignMissionListEntry('L', "2: Destroy It") == "\xC3\xBF 2: Destroy It");
        REQUIRE(campaignMissionListEntry('U', "3: Next") == "\xC3\xBD 3: Next");
    }
}
