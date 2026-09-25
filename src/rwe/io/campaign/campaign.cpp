#include "campaign.h"

namespace rwe
{
    Campaign parseCampaign(const TdfBlock& tdf)
    {
        Campaign campaign;
        if (auto header = tdf.findBlock("HEADER"))
        {
            if (auto side = header->get().findValue("campaignside"); side && !side->get().empty())
            {
                campaign.side = side->get();
            }
        }

        // MISSION%d (0x504A78) from 0 until a block is missing.
        for (int i = 0;; ++i)
        {
            auto block = tdf.findBlock("MISSION" + std::to_string(i));
            if (!block)
            {
                break;
            }
            CampaignMission mission;
            mission.block = block->get();
            if (auto file = block->get().findValue("missionfile"))
            {
                mission.missionFile = file->get();
            }
            campaign.missions.push_back(std::move(mission));
        }
        return campaign;
    }

    std::string campaignMissionName(const CampaignMission& mission, const std::string& language)
    {
        if (auto name = mission.block.findValue(language + "missionname"))
        {
            return name->get();
        }
        if (auto name = mission.block.findValue("missionname"))
        {
            return name->get();
        }
        return "Error -- Unnamed Mission";
    }
}
