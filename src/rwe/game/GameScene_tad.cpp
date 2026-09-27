#include "GameScene.h"
#include <algorithm>
#include <imgui.h>
#include <memory>
#include <optional>
#include <rwe/puppet/TadPuppetDriver.h>
#include <rwe/puppet/TadScenePlayback.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/util/SimpleLogger.h>
#include <string>

// Watching a TA demo: the map is loaded like any game, but every player is
// driven from the recorded stream rather than from orders. Split out of
// GameScene.cpp for the reason given at the head of GameScene_render.cpp.

namespace rwe
{
    void GameScene::enableTadPlayback(const std::string& path, const std::vector<std::string>& unitLoadOrder)
    {
        tadPlayback = std::make_shared<TadScenePlayback>(simulation, path, unitLoadOrder);

        // A spectator is not a player: show the whole map and every unit, and
        // let nobody issue an order. The first seat is only where the camera
        // and the interface hang.
        fogOfWarEnabled = false;
        spectatorMode = true;
        replayPlaying = true;
        replaySpeed = 1;
        replayReachedEnd = false;

        LOG_INFO << "Tad: playing " << path << " through tick " << tadPlayback->lastTick().value_or(0u);
    }

    void GameScene::applyTadTick()
    {
        if (!tadPlayback)
        {
            return;
        }

        tadPlayback->applyTick(sceneTime.value);
        for (auto& line : tadPlayback->takeChat())
        {
            printChatLine(line.player, line.ally ? "(ally) " + line.text : line.text);
        }
    }

    unsigned int GameScene::playbackLastTick() const
    {
        if (tadPlayback)
        {
            return tadPlayback->lastTick().value_or(0u);
        }
        if (replayPlayback)
        {
            return replayPlayback->lastTick;
        }
        return 0u;
    }

    void GameScene::renderTadWindow()
    {
        if (!tadPlayback)
        {
            return;
        }

        // An arena run draws nothing and ImGui has no frame open; the replay
        // window explains the same trap.
        if (gameParameters.aiArenaSeconds)
        {
            return;
        }

        auto ticksPerSecond = static_cast<unsigned int>(SimTicksPerSecond);
        auto lastTick = std::max(playbackLastTick(), 1u);
        auto nowSeconds = static_cast<int>(sceneTime.value / ticksPerSecond);
        auto endSeconds = static_cast<int>(lastTick / ticksPerSecond);
        auto atEnd = sceneTime.value >= lastTick;

        ImGui::SetNextWindowSize(ImVec2(420.0f, 150.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Demo"))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("%d:%02d of %d:%02d%s",
            nowSeconds / 60, nowSeconds % 60, endSeconds / 60, endSeconds % 60,
            atEnd ? "   (end)" : "");

        if (ImGui::Button(replayPlaying ? "Pause" : "Play"))
        {
            replayPlaying = !replayPlaying;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderInt("Speed", &replaySpeed, 1, 64, "%dx");
        for (int preset : {1, 4, 16, 64})
        {
            ImGui::SameLine();
            auto label = std::to_string(preset) + "x";
            if (ImGui::SmallButton(label.c_str()))
            {
                replaySpeed = preset;
            }
        }

        const auto& s = tadPlayback->stats();
        ImGui::Text("shots %llu/%llu  scripts %llu/%llu  chat %llu+%llu",
            static_cast<unsigned long long>(s.shotsSpawned),
            static_cast<unsigned long long>(s.shotsDropped),
            static_cast<unsigned long long>(s.scriptCallsRun),
            static_cast<unsigned long long>(s.scriptCallsDropped),
            static_cast<unsigned long long>(s.chatLines),
            static_cast<unsigned long long>(s.allyChatLines));

        ImGui::End();
    }
}
