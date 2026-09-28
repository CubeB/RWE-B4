#include "TaHostLobbyScene.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <imgui.h>
#include <rwe/game/PlayerColorIndex.h>
#include <rwe/game/GameSimulationLoader.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/tnt/TntArchive.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/net/ta/TaHostGame.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/Metal.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/SpanStream.h>
#include <string>
#include <vector>

// The panel in front of a hosted game. The host itself is in TaHostGame; this
// is the screen that shows it and starts the map once it has launched.

namespace rwe
{
    namespace
    {
        std::string sideName(TadSide side)
        {
            switch (side)
            {
                case TadSide::Core:
                    return "CORE";
                case TadSide::Arm:
                    return "ARM";
                default:
                    // A joiner that has not said is shown on the other side
                    // rather than as neither: the game has to seat it and the
                    // two players cannot both be on one side.
                    return "ARM";
            }
        }

        std::string teamName(std::uint8_t team)
        {
            return team == TaNoTeam ? "none" : std::to_string(static_cast<int>(team));
        }

        /**
         * The data set's unit types in TA's order, which is what a 0x09's type
         * index is a position in. Read from the units directory rather than
         * from a loaded game: the launch is sent before the map is loaded, and
         * the listing is a directory read.
         */
        std::vector<std::string> unitLoadOrder(const SceneContext& context)
        {
            auto fbis = context.vfs->getFileNames(context.pathMapping->units, ".fbi");
            std::vector<std::string> stems;
            stems.reserve(fbis.size());
            for (const auto& name : fbis)
            {
                stems.push_back(std::filesystem::path(name).stem().string());
            }
            return tadUnitLoadOrder(std::move(stems));
        }

        /** The 1-based position of a unit type in the listing, or zero. */
        unsigned int typeIndexOf(const std::vector<std::string>& loadOrder, const std::string& name)
        {
            auto it = std::find(loadOrder.begin(), loadOrder.end(), name);
            if (it == loadOrder.end())
            {
                return 0;
            }
            return static_cast<unsigned int>(it - loadOrder.begin()) + 1;
        }
    }

    TaHostLobbyScene::TaHostLobbyScene(
        const SceneContext& sceneContext,
        TdfBlock* audioLookup,
        AudioService::LoopToken&& bgm,
        std::shared_ptr<TaHostGame> host,
        std::string mapName,
        unsigned int schemaIndex)
        : sceneContext(sceneContext),
          audioLookup(audioLookup),
          bgm(std::move(bgm)),
          host(std::move(host)),
          mapName(std::move(mapName)),
          schemaIndex(schemaIndex)
    {
    }

    void TaHostLobbyScene::init()
    {
        if (!host)
        {
            LOG_ERROR << "TA host: there is no host to wait for";
            sceneContext.sceneManager->requestExit();
            return;
        }
        const auto& config = host->config();

        // Read once: the commander's type index is its position in the data
        // set's unit listing and its position is the map's first start
        // position, and neither of those changes while a panel is up. A frame
        // that re-read the listing would be a directory scan sixty times a
        // second.
        params = computeLaunchParams();
        if (!params)
        {
            auto side = std::string(config.side == TadSide::Core ? "CORE" : "ARM");
            LOG_ERROR << "TA host: " << mapName << " has no first start position, or the data set's unit"
                      << "listing does not name " << side << "'s commander; nothing can be launched";
        }
    }

    void TaHostLobbyScene::update(int)
    {
        // ImGui windows go in during update: the scene manager ends the ImGui
        // frame before it calls render(), so a window begun there never draws.
        drawPanel();

        if (!host || loading || !params)
        {
            return;
        }

        host->autoLaunchIfReady(*params);

        auto lobby = host->lobbyState();
        if (lobby.launched && !launched)
        {
            launched = Launched{};
            launched->name = lobby.launched->name;
            launched->team = lobby.launched->team;

            // A joiner whose own status named no side is put on the other one:
            // a player on no side cannot be seated on a map, and ours is the
            // one side this host knows.
            auto side = lobby.launched->side;
            if (side != TadSide::Arm && side != TadSide::Core)
            {
                side = host->config().side == TadSide::Arm ? TadSide::Core : TadSide::Arm;
            }
            launched->side = sideName(side);
            launched->color = PlayerColorIndex(std::min<unsigned int>(lobby.launched->colour, 9u));
            launch();
        }
    }

    std::optional<TaBattleroom::LaunchParams> TaHostLobbyScene::computeLaunchParams() const
    {
        const auto& config = host->config();
        auto side = std::string(config.side == TadSide::Core ? "CORE" : "ARM");
        auto it = sceneContext.sideData->find(side);
        if (it == sceneContext.sideData->end())
        {
            return std::nullopt;
        }

        auto index = typeIndexOf(unitLoadOrder(sceneContext), it->second.commander);
        if (index == 0)
        {
            return std::nullopt;
        }

        auto otaBytes = sceneContext.vfs->readFile(std::string("maps/").append(mapName).append(".ota"));
        if (!otaBytes)
        {
            return std::nullopt;
        }
        auto ota = parseOta(parseTdfFromString(std::string(otaBytes->begin(), otaBytes->end())));
        if (schemaIndex >= ota.schemas.size())
        {
            return std::nullopt;
        }
        auto startPos = findStartPosition(ota.schemas.at(schemaIndex), 1);
        if (!startPos)
        {
            return std::nullopt;
        }

        TaBattleroom::LaunchParams params;
        params.commanderTypeIndex = static_cast<std::uint16_t>(index);
        params.commanderUnitId = host->commanderUnitId();
        // A real host's 0x09 carries the ground's height (86 at Canal
        // Crossing's first start), so this one does too. The OTA's start
        // position is in TA's frame, from the map's corner; the terrain's is
        // centred, hence the half-extents.
        SimScalar height = 0_ss;
        if (auto tntBytes = sceneContext.vfs->readFile(std::string("maps/").append(mapName).append(".tnt")))
        {
            SpanStream tntStream(tntBytes->data(), tntBytes->size());
            TntArchive tnt(&tntStream);
            auto mapData = readMapData(tnt, ota, schemaIndex);
            const auto& terrain = mapData.terrain;
            height = terrain.getHeightAt(
                SimScalar(startPos->xPos) - terrain.getWidthInWorldUnits() / 2_ss,
                SimScalar(startPos->zPos) - terrain.getHeightInWorldUnits() / 2_ss);
        }
        params.commanderPosition = TadPosition{
            simScalarToFixed(SimScalar(startPos->xPos)),
            simScalarToFixed(height),
            simScalarToFixed(SimScalar(startPos->zPos))};
        return params;
    }

    void TaHostLobbyScene::tryLaunch()
    {
        if (!host || loading || !params)
        {
            return;
        }
        host->launch(*params);
    }

    void TaHostLobbyScene::onKeyDown(const SDL_KeyboardEvent& event)
    {
        if (event.key == SDLK_RETURN || event.key == SDLK_KP_ENTER)
        {
            tryLaunch();
        }
    }

    void TaHostLobbyScene::launch()
    {
        // The launch has been sent and the joiner is in game; the map loads
        // under the parameters the two of them agreed on.
        startLoading();
    }

    void TaHostLobbyScene::startLoading()
    {
        if (loading)
        {
            return;
        }
        loading = true;

        const auto& config = host->config();
        GameParameters parameters{mapName, schemaIndex};
        parameters.netMode = NetMode::OwnClock;
        parameters.taHostGame = host;

        // TA has no commander-death rule: a player whose commander dies keeps
        // playing, and the peer owns the other half of the board besides.
        parameters.commanderDeath = CommanderDeathMode::GameContinues;

        auto localSide = std::string(config.side == TadSide::Core ? "CORE" : "ARM");
        auto localColor = std::min<unsigned int>(config.colour, 9u);
        PlayerInfo localPlayer{
            config.gameName,
            PlayerControllerTypeHuman{},
            localSide,
            PlayerColorIndex(localColor),
            Metal(1000),
            Energy(1000)};
        if (config.team != TaNoTeam)
        {
            localPlayer.teamId = static_cast<int>(config.team);
        }
        parameters.players[0] = localPlayer;

        // The joiner is a Remote player: TA owns its units and sends their
        // state, so RWE simulates none of them. It is a network player so that
        // the loader leaves it to somebody else, and the loading scene skips
        // the lockstep endpoint for a game that has no lockstep in it.
        PlayerInfo joiner{
            launched->name,
            PlayerControllerTypeNetwork{},
            launched->side,
            launched->color,
            Metal(1000),
            Energy(1000)};
        if (launched->team != TaNoTeam)
        {
            joiner.teamId = static_cast<int>(launched->team);
        }
        parameters.players[TaHostGame::remotePlayerSlot] = joiner;

        LOG_INFO << "TA host: loading " << mapName << " with " << localSide << " against " << launched->side
                 << " (colour " << static_cast<int>(launched->color.value) << ", team " << teamName(launched->team) << ")";

        sceneContext.audioService->stopMusic();
        auto scene = std::make_unique<LoadingScene>(
            sceneContext,
            audioLookup,
            AudioService::LoopToken(),
            std::move(parameters));
        sceneContext.sceneManager->setNextScene(std::shared_ptr<Scene>(std::move(scene)));
    }

    void TaHostLobbyScene::drawPanel()
    {
        if (!host)
        {
            return;
        }

        auto lobby = host->lobbyState();

        ImGui::SetNextWindowSize(ImVec2(520.0f, 260.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Hosting a Total Annihilation game"))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("map: %s", mapName.c_str());
        ImGui::Text("session: %s", host->config().gameName.c_str());
        ImGui::Text(
            "ports: enum %u, game tcp %u, udp %u",
            lobby.enumPort,
            lobby.gameTcpPort,
            lobby.gameUdpPort);
        ImGui::Separator();

        if (lobby.peers.empty())
        {
            ImGui::TextWrapped("Waiting for a player to join...");
        }
        else
        {
            if (ImGui::BeginTable("joiners", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("name");
                ImGui::TableSetupColumn("side");
                ImGui::TableSetupColumn("colour");
                ImGui::TableSetupColumn("team");
                ImGui::TableSetupColumn("ready");
                ImGui::TableHeadersRow();

                for (const auto& peer : lobby.peers)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(peer.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(sideName(peer.side).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", static_cast<int>(peer.colour));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(teamName(peer.team).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(peer.ready ? "yes" : "no");
                }
                ImGui::EndTable();
            }
        }

        if (!lobby.refusal.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "refused: %s", lobby.refusal.c_str());
        }

        if (launched)
        {
            ImGui::Text("launched; loading the map...");
        }
        else
        {
            ImGui::Separator();
            if (ImGui::Button("Launch (Enter)", ImVec2(160.0f, 0.0f)))
            {
                tryLaunch();
            }
        }

        ImGui::End();
    }
}
