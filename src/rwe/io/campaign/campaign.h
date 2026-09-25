#pragma once

#include <rwe/io/tdf/TdfBlock.h>
#include <string>
#include <vector>

namespace rwe
{
    /** One [MISSIONn] of a campaign file. */
    struct CampaignMission
    {
        /** The map, `Maps\<missionfile>` with the OTA extension (0x435F5E, 0x4290F0). Empty when the entry names none. */
        std::string missionFile;

        /**
         * The block, kept so the localised name can be looked up:
         * `missionname` is read through the localised-key reader 0x4C58A0,
         * which puts the language string in front of the key.
         */
        TdfBlock block;
    };

    /** A campaign file, `camps\<name>.tdf` (0x476AE0, 0x4356C0), TOTALA-EXE-DATA.md S:105. */
    struct Campaign
    {
        /** `[HEADER] campaignside`, "ALL" when absent (0x476BB1). */
        std::string side{"ALL"};
        std::vector<CampaignMission> missions;
    };

    /** Reads [HEADER] and then [MISSION0], [MISSION1], ... until one is missing. */
    Campaign parseCampaign(const TdfBlock& tdf);

    /**
     * A mission's name in a language: `<language>missionname`, where the
     * English language string is empty so the key is plain `missionname`.
     * The original shows "Error -- Unnamed Mission" (0x504A84) when the key
     * is missing; falling back from a missing translation to the plain key
     * first is RWE's own.
     */
    std::string campaignMissionName(const CampaignMission& mission, const std::string& language);
}
