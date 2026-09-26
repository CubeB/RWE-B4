#include "MainMenuScene.h"
#include <algorithm>
#include <rwe/LoadingScene.h>
#include <rwe/io/campaign/campaign.h>
#include <rwe/io/gui/gui.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/ui/UiLabel.h>
#include <rwe/ui/UiListBox.h>
#include <rwe/ui/UiStagedButton.h>
#include <rwe/ui/UiTextRegion.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/rwe_string.h>

/**
 * The campaign's first screen, NEWGAME.GUI, as the original runs it from the
 * single-player menu's NewCamp (TOTALA-EXE-DATA.md §115): a side, a
 * difficulty, a campaign of that side and any of its missions.
 */
namespace rwe
{
    AiDifficulty skirmishDifficultyToAiDifficulty(unsigned int stage);

    namespace
    {
        /** SIDEDATA's names for the two sides, which `campaignside` is compared against. */
        const char* const campaignSideNames[2] = {"ARM", "CORE"};

        /** A planet as the briefing draws it: its GAF, the panorama and the turning globe. */
        struct BriefingPlanet
        {
            const char* gaf;
            const char* panorama;
            const char* rotation;
        };

        /**
         * The mission's `planet=` to its art (the table at 0x4790E6). The
         * sequence names are the GAFs' own, which do not all follow one
         * pattern. A name the table does not know is the green planet, and a
         * Core player's Lunar is Lunar2 (0x479072-0x4790DB).
         */
        BriefingPlanet briefingPlanet(const std::string& planet, bool core)
        {
            static const std::pair<const char*, BriefingPlanet> table[] = {
                {"GREEN PLANET", {"Greenbrief", "GreenPan", "GreenRotate"}},
                {"ARCHIPELAGO", {"Archibrief", "ArchiPan", "ArchiRotate"}},
                {"WET DESERT", {"WDesertbrief", "WDesPan", "WDesertRotate"}},
                {"DESERT", {"Desertbrief", "DDesPan", "DDesRotate"}},
                {"LAVA", {"Lavabrief", "LavaPan", "LavaRotate"}},
                {"RED PLANET", {"Marsbrief", "MarsPan", "MarsRotate"}},
                {"LUNAR", {"Lunarbrief", "LunarPan", "LunarRotate"}},
                {"METAL", {"Metalbrief", "MetalPan", "MetalRotate"}},
                {"LUNAR2", {"Lunar2Brief", "Lunar2Pan", "Lunar2Rotate"}},
                {"ICE", {"ICEBRIEF", "IcePan", "Icerotate"}},
                {"LUSH", {"Lushbrief", "LushPan", "Lushrotate"}},
                {"SLATE", {"Slatebrief", "SlatePan", "Slaterotate"}},
                {"WATER WORLD", {"Waterbrief", "WaterPan", "Waterrotate"}},
                {"ACID", {"Acidbrief", "AcidPan", "Acidrotate"}},
                {"CRYSTAL", {"Crystalbrief", "CrystPan", "Crystalrotate"}},
            };
            auto wanted = toUpper(planet);
            if (core && wanted == "LUNAR")
            {
                wanted = "LUNAR2";
            }
            for (const auto& [name, art] : table)
            {
                if (wanted == name)
                {
                    return art;
                }
            }
            return table[0].second;
        }
    }

    std::vector<std::string> MainMenuScene::campaignNamesForSide(unsigned int side)
    {
        // 0x476A60: every camps\*.tdf whose [HEADER] campaignside is the
        // side's name or ALL.
        std::vector<std::string> names;
        for (auto file : sceneContext.vfs->getFileNames("camps", ".tdf"))
        {
            auto raw = sceneContext.vfs->readFile("camps/" + file);
            if (!raw)
            {
                continue;
            }
            Campaign campaign;
            try
            {
                campaign = parseCampaign(parseTdfFromString(std::string(raw->begin(), raw->end())));
            }
            catch (const std::exception& e)
            {
                LOG_WARN << "Campaign file " << file << " could not be read: " << e.what();
                continue;
            }
            auto campaignSide = toUpper(campaign.side);
            if (campaignSide == "ALL" || campaignSide == campaignSideNames[side])
            {
                file.resize(file.size() - 4);
                names.push_back(file);
            }
        }
        return names;
    }

    void MainMenuScene::goToCampaignMenu()
    {
        auto raw = sceneContext.vfs->readFile(sceneContext.pathMapping->guis + "/NEWGAME.GUI");
        if (!raw)
        {
            openMessageBox("Couldn't read NEWGAME.GUI");
            return;
        }
        auto entries = parseGuiFromBytes(*raw);
        if (!entries)
        {
            openMessageBox("Couldn't read NEWGAME.GUI");
            return;
        }

        // The "play any mission" mode, the only one v3.1 reaches: both lists
        // shown, the campaigns' squeezed in above the missions'
        // (0x478303-0x478385).
        for (auto& entry : *entries)
        {
            const auto& name = entry.common.name;
            if (name == "Campaign" || name == "CampaignKnob")
            {
                entry.common.ypos = 308;
                entry.common.height = 48;
                entry.common.active = 1;
            }
            else if (name == "Missions" || name == "MissionsKnob")
            {
                entry.common.height = 62;
                entry.common.active = 1;
            }
        }

        auto panel = uiFactory.panelFromGuiFile("NEWGAME", "playanygame4", *entries);
        if (auto list = panel->find<UiListBox>("Campaign"))
        {
            // Choosing a campaign lists its missions (0x4779E0).
            auto sub = list->get().selectedIndex().subscribe([this](const std::optional<unsigned int>& index) {
                if (index)
                {
                    fillCampaignMissions(*index);
                }
            });
            list->get().addSubscription(std::move(sub));
        }
        goToMenu(std::move(panel));
        refreshCampaignMenu();
    }

    void MainMenuScene::refreshCampaignMenu()
    {
        if (panelStack.empty())
        {
            return;
        }
        auto& panel = *panelStack.back();

        for (unsigned int side = 0; side < 2; ++side)
        {
            if (auto picture = panel.find<UiStagedButton>(side == 0 ? "Side0" : "Side1"))
            {
                // The GAF's pressed face is the dark one, so the chosen side
                // is the one left unpressed and lit.
                picture->get().setToggledOn(campaignSide != side);
            }
        }
        if (auto label = panel.find<UiLabel>("SIDENAME"))
        {
            label->get().setText(campaignSide == 0 ? "Arm" : "Core");
        }
        if (auto difficulty = panel.find<UiStagedButton>("Difficulty"))
        {
            difficulty->get().setStage(campaignDifficulty);
        }

        campaignNames = campaignNamesForSide(campaignSide);
        if (auto list = panel.find<UiListBox>("Campaign"))
        {
            auto& campaigns = list->get();
            campaigns.clearItems();
            for (const auto& name : campaignNames)
            {
                campaigns.appendItem(name);
            }
            if (!campaignNames.empty())
            {
                campaigns.setSelectedItem(campaignNames.front());
            }
        }
        fillCampaignMissions(0);
    }

    void MainMenuScene::fillCampaignMissions(unsigned int campaignIndex)
    {
        selectedCampaign = campaignIndex < campaignNames.size() ? std::optional<std::string>(campaignNames[campaignIndex]) : std::nullopt;
        if (panelStack.empty())
        {
            return;
        }
        auto list = panelStack.back()->find<UiListBox>("Missions");
        if (!list)
        {
            return;
        }
        auto& missions = list->get();
        missions.clearItems();
        if (!selectedCampaign)
        {
            return;
        }
        if (auto campaign = readCampaign(*sceneContext.vfs, *selectedCampaign))
        {
            for (const auto& mission : campaign->missions)
            {
                missions.appendItem(campaignMissionName(mission, std::string()));
            }
            if (!campaign->missions.empty())
            {
                missions.setSelectedItem(missions.getItems().front());
            }
        }
    }

    void MainMenuScene::campaignMenuMessage(const std::string& message)
    {
        if (message == "Side0" || message == "Arm")
        {
            campaignSide = 0;
            refreshCampaignMenu();
        }
        else if (message == "Side1" || message == "Core")
        {
            campaignSide = 1;
            refreshCampaignMenu();
        }
        else if (message == "Difficulty")
        {
            // The button steps its own face; this follows it (0x477CFB).
            campaignDifficulty = (campaignDifficulty + 1) % 3;
        }
        else if (message == "Start")
        {
            startCampaignMission();
        }
    }

    void MainMenuScene::startCampaignMission()
    {
        if (!selectedCampaign || panelStack.empty())
        {
            return;
        }
        std::size_t missionIndex = 0;
        if (auto list = panelStack.back()->find<UiListBox>("Missions"))
        {
            missionIndex = list->get().getSelectedIndex().value_or(0);
        }

        // A campaign started from this screen starts with every mission
        // untried (0x41DA30).
        CampaignProgress progress;
        progress.campaign = *selectedCampaign;
        progress.missionIndex = static_cast<unsigned int>(missionIndex);
        progress.difficulty = campaignDifficulty;
        progress.side = campaignSide;
        openCampaignMission(progress);
    }

    void MainMenuScene::openCampaignMission(CampaignProgress progress)
    {
        auto campaign = readCampaign(*sceneContext.vfs, progress.campaign);
        if (!campaign || campaign->missions.empty())
        {
            openMessageBox("That campaign has no missions");
            return;
        }
        if (progress.missionIndex >= campaign->missions.size())
        {
            return;
        }
        const auto& mission = campaign->missions[progress.missionIndex];

        auto mapName = campaignMissionMapName(mission);
        auto otaRaw = sceneContext.vfs->readFile("maps/" + mapName + ".ota");
        if (!otaRaw)
        {
            openMessageBox("Could not read the mission " + mapName);
            return;
        }
        // A mission map whose OTA does not parse is a message, not a crash, as
        // it is in the skirmish map list (setCandidateSelectedMap).
        OtaRecord ota;
        try
        {
            ota = parseOta(parseTdfFromString(std::string(otaRaw->begin(), otaRaw->end())));
        }
        catch (const std::exception& e)
        {
            LOG_ERROR << "Could not read mission " << mapName << ": " << e.what();
            openMessageBox("Could not read the mission " + mapName + ": " + e.what());
            return;
        }
        auto schema = chooseCampaignSchema(ota.schemas, static_cast<int>(progress.difficulty));
        if (!schema)
        {
            openMessageBox("No suitable schema type");
            return;
        }

        // What the screens after the game will want to know.
        progress.glamour = ota.glamour;
        progress.glamourSound = ota.glamourSound;
        progress.noMovie = ota.noMovie;
        progress.hasNextMission = progress.missionIndex + 1 < campaign->missions.size();

        GameParameters params{mapName, static_cast<unsigned int>(*schema)};
        params.mission = true;
        params.aiDifficulty = skirmishDifficultyToAiDifficulty(progress.difficulty);
        // The player is player 0 on the chosen side in colour 0, the
        // computer player 1 on the other in colour 1 (0x477C79, 0x477C8B);
        // what each starts with is the mission's, which the loader reads.
        params.players[0] = PlayerInfo{std::nullopt, PlayerControllerTypeHuman(), campaignSideNames[progress.side], PlayerColorIndex(0), Metal(0), Energy(0)};
        params.players[1] = PlayerInfo{std::nullopt, PlayerControllerTypeComputer(), campaignSideNames[1 - progress.side], PlayerColorIndex(1), Metal(0), Energy(0)};
        params.campaign = progress;

        goToCampaignBriefing(params, ota);
    }

    void MainMenuScene::resumeCampaign(const CampaignProgress& progress)
    {
        pendingCampaign = progress;
    }

    void MainMenuScene::goToCampaignBriefing(const GameParameters& params, const OtaRecord& ota)
    {
        auto raw = sceneContext.vfs->readFile(sceneContext.pathMapping->guis + "/MSNBRIEF.GUI");
        auto entries = raw ? parseGuiFromBytes(*raw) : std::nullopt;
        if (!entries)
        {
            // Without the screen, straight into the game rather than nowhere.
            briefedGame = params;
            launchBriefedGame();
            return;
        }

        auto sideIndex = params.campaign ? params.campaign->side : campaignSide;
        const auto& side = sceneContext.sideData->at(campaignSideNames[sideIndex]);
        auto panel = uiFactory.panelFromGuiFile("MSNBRIEF", "mbrief" + side.namePrefix, *entries);

        // The text, in the side's font (FONT gadget side+1, 0x476DC7), a page
        // at a time in TextRegion's rectangle.
        auto font = sceneContext.textureService->getFont(sideIndex == 0 ? "fonts/ARMFONT.FNT" : "fonts/COREFONT.FNT");
        for (const auto& entry : *entries)
        {
            const auto& c = entry.common;
            if (c.name == "TextRegion")
            {
                auto region = std::make_unique<UiTextRegion>(c.xpos, c.ypos, static_cast<unsigned int>(c.width), static_cast<unsigned int>(c.height), font);
                region->setName("TextRegion");
                if (auto text = sceneContext.vfs->readFile(campaignResourcePath("camps/briefs", ota.brief, ".txt")))
                {
                    region->setText(std::string(text->begin(), text->end()));
                }
                panel->removeChildrenNamed("TextRegion");
                panel->appendChild(std::move(region));
            }
            else if (c.name == "PLANET")
            {
                // The globe turns. The panorama is its first strip fitted to
                // the window, where the original scrolls the strips across it
                // (0x478790) and prints the wind and gravity over them.
                auto art = briefingPlanet(ota.planet, sideIndex == 1);
                auto gaf = std::string("anims/") + art.gaf + ".GAF";
                // The art is decoration: a mod's planet with no GAF shows none.
                auto sequence = [&](const char* name) -> std::shared_ptr<SpriteSeries> {
                    try
                    {
                        return sceneContext.textureService->getGafEntry(gaf, name);
                    }
                    catch (const std::exception&)
                    {
                        LOG_WARN << "Briefing art " << gaf << " has no " << name;
                        return nullptr;
                    }
                };
                if (auto globe = sequence(art.rotation))
                {
                    panel->appendChild(std::make_unique<UiAnimation>(c.xpos, c.ypos, static_cast<unsigned int>(c.width), static_cast<unsigned int>(c.height), globe, 15.0f));
                }
                for (const auto& other : *entries)
                {
                    if (other.common.name == "PANORAMA")
                    {
                        if (auto pan = sequence(art.panorama))
                        {
                            const auto& p = other.common;
                            panel->appendChild(std::make_unique<UiAnimation>(p.xpos, p.ypos, static_cast<unsigned int>(p.width), static_cast<unsigned int>(p.height), pan, 0.0f, true));
                        }
                    }
                }
            }
        }
        if (auto shutUp = panel->find<UiStagedButton>("SHUTUP"))
        {
            shutUp->get().setStage(1);
        }

        briefedGame = params;
        goToMenu(std::move(panel));

        // The narration plays as the briefing opens (0x476E56).
        narration = sceneContext.audioService->loadSoundFromPath(campaignResourcePath("camps/briefs", ota.narration, ".wav"));
        startNarration();
    }

    void MainMenuScene::startNarration()
    {
        stopNarration();
        if (narration)
        {
            narrationChannel = sceneContext.audioService->playSound(*narration);
        }
    }

    void MainMenuScene::stopNarration()
    {
        sceneContext.audioService->stopChannel(narrationChannel);
        narrationChannel = -1;
    }

    void MainMenuScene::campaignBriefingMessage(const std::string& message)
    {
        if (message == "Start")
        {
            launchBriefedGame();
        }
        else if (message == "SHUTUP")
        {
            // No Narration | Narration: off stops it, on plays it again.
            if (narrationChannel >= 0)
            {
                stopNarration();
            }
            else
            {
                startNarration();
            }
        }
    }

    void MainMenuScene::launchBriefedGame()
    {
        stopNarration();
        if (!briefedGame)
        {
            return;
        }
        sceneContext.audioService->stopMusic();
        auto scene = std::make_unique<LoadingScene>(sceneContext, soundLookup, std::move(bgm), *briefedGame);
        briefedGame.reset();
        sceneContext.sceneManager->setNextScene(std::shared_ptr<Scene>(std::move(scene)));
    }
}
