#pragma once

#include <nlohmann/json.hpp>
#include <rwe/sim/GamePlayerInfo.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapFeature.h>
#include <rwe/sim/MissionRules.h>
#include <rwe/sim/MissionScripts.h>

namespace rwe
{
    /**
     * The player, mission and feature halves of the save format. They were
     * file-private helpers in save_util.cpp until that object crossed the
     * COFF section budget (2026-11-03, 24979 of the 24575 allowed): each is
     * a json builder over a variant or a vector, which is exactly the dense
     * code the budget charges for. Moving them costs this translation unit
     * the standard library's floor once and gives save_util.cpp back every
     * instantiation they made. Only save_util.cpp and this file's own
     * definition include this header.
     */
    nlohmann::json saveWinStatus(const WinStatus& s);
    WinStatus loadWinStatus(const nlohmann::json& j);

    nlohmann::json saveMissionRule(const MissionRule& r);
    MissionRule loadMissionRule(const nlohmann::json& j);
    nlohmann::json saveMissionRules(const MissionRules& m);
    MissionRules loadMissionRules(const nlohmann::json& j, std::size_t playerCount);

    nlohmann::json saveMissionScripts(const MissionScripts& m);
    MissionScripts loadMissionScripts(const nlohmann::json& j);

    nlohmann::json saveGamePlayerInfo(const GamePlayerInfo& p);
    GamePlayerInfo loadGamePlayerInfo(const nlohmann::json& j);

    nlohmann::json saveMapFeature(const MapFeature& f);
}
