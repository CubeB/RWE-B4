#pragma once

#include <optional>
#include <rwe/io/tdf/TdfBlock.h>
#include <string>
#include <vector>

namespace rwe
{
    class AbstractVirtualFileSystem;

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

    /**
     * The map a mission is played on: its `missionfile` without the `.ota`
     * the campaign files write (EXP1AC01.ota), a game being started by the
     * map's bare name.
     */
    std::string campaignMissionMapName(const CampaignMission& mission);

    /**
     * Where a mission's named resource lives -- its brief, narration, glamour
     * picture or glamour sound -- as the original's resolver 0x435430 makes
     * it: `directory/name` cut at the name's last '.' (0x4BB0F0), then the
     * extension. So `I09Brief.txt` and `I09Brief` both come to
     * `camps/briefs/I09Brief.txt`; 105 of the shipped missions write the
     * name with its extension.
     */
    std::string campaignResourcePath(const std::string& directory, const std::string& name, const std::string& extension);

    /** `camps/<name>.tdf`, or nothing if it is missing or will not parse. */
    std::optional<Campaign> readCampaign(AbstractVirtualFileSystem& vfs, const std::string& name);

    /**
     * A mission as the list between missions shows it (0x41EAA0): a thumb for
     * how it went, then a space and the name. `status` is the mission's
     * letter in the campaign's run, W won, L lost, U not yet played, and the
     * thumbs are the last three glyphs of the list's font: 0xFE up for a win,
     * 0xFF down for a loss, 0xFD an empty box. They are written as the code
     * points they are, the text renderer reading UTF-8.
     */
    std::string campaignMissionListEntry(char status, const std::string& name);
}
