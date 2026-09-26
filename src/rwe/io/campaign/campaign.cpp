#include "campaign.h"

#include <exception>
#include <rwe/io/tdf/tdf.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/rwe_string.h>
#include <rwe/vfs/AbstractVirtualFileSystem.h>

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

    std::string campaignMissionMapName(const CampaignMission& mission)
    {
        auto name = mission.missionFile;
        if (name.size() > 4 && toUpper(name.substr(name.size() - 4)) == ".OTA")
        {
            name.resize(name.size() - 4);
        }
        return name;
    }

    std::optional<Campaign> readCampaign(AbstractVirtualFileSystem& vfs, const std::string& name)
    {
        auto raw = vfs.readFile("camps/" + name + ".tdf");
        if (!raw)
        {
            return std::nullopt;
        }
        try
        {
            return parseCampaign(parseTdfFromString(std::string(raw->begin(), raw->end())));
        }
        catch (const std::exception& e)
        {
            LOG_WARN << "Campaign " << name << " could not be read: " << e.what();
            return std::nullopt;
        }
    }

    std::string campaignMissionListEntry(char status, const std::string& name)
    {
        // U+00FD, U+00FE and U+00FF in UTF-8.
        const char* thumb = status == 'W' ? "\xC3\xBE" : (status == 'L' ? "\xC3\xBF" : "\xC3\xBD");
        return std::string(thumb) + " " + name;
    }
}
