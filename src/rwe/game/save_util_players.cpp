#include "save_util_players.h"

#include "SaveJson.h"
#include <rwe/util/match.h>

namespace rwe
{
    using nlohmann::json;

    json saveWinStatus(const WinStatus& s)
    {
        return match(
            s,
            [](const WinStatusWon& w) { return json{{"kind", "won"}, {"winner", w.winner.value}}; },
            [](const WinStatusNoSurvivors&) { return json{{"kind", "nosurvivors"}}; },
            [](const WinStatusUndecided&) { return json{{"kind", "undecided"}}; });
    }

    WinStatus loadWinStatus(const json& j)
    {
        const auto& kind = j.at("kind").get_ref<const std::string&>();
        if (kind == "won")
        {
            return WinStatusWon{PlayerId(j.at("winner").get<unsigned int>())};
        }
        // "draw" is what saves written before BT-191 call it.
        if (kind == "nosurvivors" || kind == "draw")
        {
            return WinStatusNoSurvivors();
        }
        if (kind == "undecided")
        {
            return WinStatusUndecided();
        }
        throw std::runtime_error("bad WinStatus kind: " + kind);
    }

    // ---- mission rules ----------------------------------------------

    json saveMissionRule(const MissionRule& r)
    {
        return json{
            {"kind", saveEnum(r.kind)},
            {"unitType", r.unitType},
            {"number", r.number},
            {"x", saveSimScalar(r.x)},
            {"z", saveSimScalar(r.z)},
            {"radius", saveSimScalar(r.radius)},
            {"satisfied", r.satisfied},
            {"celebrated", r.celebrated},
        };
    }

    MissionRule loadMissionRule(const json& j)
    {
        MissionRule r;
        r.kind = loadEnum<MissionRule::Kind>(j.at("kind"));
        if (r.kind > MissionRule::Kind::AnyUnitPassesZ)
        {
            throw std::runtime_error("bad mission rule kind");
        }
        r.unitType = j.at("unitType").get<std::string>();
        r.number = j.at("number").get<int>();
        r.x = loadSimScalar(j.at("x"));
        r.z = loadSimScalar(j.at("z"));
        r.radius = loadSimScalar(j.at("radius"));
        r.satisfied = j.at("satisfied").get<bool>();
        r.celebrated = j.at("celebrated").get<bool>();
        return r;
    }

    json saveMissionRules(const MissionRules& m)
    {
        json victory = json::array();
        for (const auto& r : m.victory)
        {
            victory.push_back(saveMissionRule(r));
        }
        json defeat = json::array();
        for (const auto& r : m.defeat)
        {
            defeat.push_back(saveMissionRule(r));
        }
        auto savePlayer = [](PlayerId p) { return json(p.value); };
        return json{
            {"victory", victory},
            {"defeat", defeat},
            {"enabled", m.enabled},
            {"countdown", m.countdown},
            {"outcome", saveOptional(m.outcome, [](MissionOutcome o) { return saveEnum(o); })},
            {"human", saveOptional(m.human, savePlayer)},
            {"computer", saveOptional(m.computer, savePlayer)},
            {"humanCommander", m.humanCommander},
            {"computerCommander", m.computerCommander},
        };
    }

    MissionRules loadMissionRules(const json& j, std::size_t playerCount)
    {
        MissionRules m;
        for (const auto& r : j.at("victory"))
        {
            m.victory.push_back(loadMissionRule(r));
        }
        for (const auto& r : j.at("defeat"))
        {
            m.defeat.push_back(loadMissionRule(r));
        }
        m.enabled = j.at("enabled").get<bool>();
        m.countdown = j.at("countdown").get<int>();
        m.outcome = loadOptional(j.at("outcome"), [](const json& v) { return loadEnum<MissionOutcome>(v); });
        // The rules look players up by id, so an id the save does not
        // seat is refused here rather than trusted.
        auto loadPlayer = [&](const json& v) {
            auto id = v.get<unsigned int>();
            if (id >= playerCount)
            {
                throw std::runtime_error("mission rules name a player the save does not have");
            }
            return PlayerId(id);
        };
        m.human = loadOptional(j.at("human"), loadPlayer);
        m.computer = loadOptional(j.at("computer"), loadPlayer);
        m.humanCommander = j.at("humanCommander").get<std::string>();
        m.computerCommander = j.at("computerCommander").get<std::string>();
        return m;
    }

    // ---- mission scripts --------------------------------------------

    json saveMissionScripts(const MissionScripts& m)
    {
        json scripts = json::array();
        for (const auto& [id, script] : m.scripts)
        {
            json steps = json::array();
            for (const auto& step : script.steps)
            {
                steps.push_back(json{
                    {"kind", saveEnum(step.kind)},
                    {"position", saveSimVector(step.position)},
                    {"unitType", step.unitType},
                    {"target", saveOptional(step.target, [](UnitId u) { return json(u.value); })},
                    {"count", step.count},
                    {"ticks", step.ticks},
                    {"radius", step.radius},
                    {"hit", step.hit},
                });
            }
            scripts.push_back(json{
                {"unit", id},
                {"started", script.started},
                {"wakeAt", saveGameTime(script.wakeAt)},
                {"steps", steps},
            });
        }
        return scripts;
    }

    MissionScripts loadMissionScripts(const json& j)
    {
        MissionScripts m;
        for (const auto& sj : j)
        {
            MissionScript script;
            script.started = sj.at("started").get<bool>();
            script.wakeAt = loadGameTime(sj.at("wakeAt"));
            for (const auto& stepJson : sj.at("steps"))
            {
                MissionStep step;
                step.kind = loadEnum<MissionStep::Kind>(stepJson.at("kind"));
                if (step.kind > MissionStep::Kind::MakeSelectable)
                {
                    throw std::runtime_error("bad mission step kind");
                }
                step.position = loadSimVector(stepJson.at("position"));
                step.unitType = stepJson.at("unitType").get<std::string>();
                step.target = loadOptional(stepJson.at("target"), [](const json& v) { return UnitId(v.get<unsigned int>()); });
                step.count = stepJson.at("count").get<int>();
                step.ticks = stepJson.at("ticks").get<int>();
                step.radius = stepJson.at("radius").get<int>();
                step.hit = stepJson.at("hit").get<bool>();
                script.steps.push_back(std::move(step));
            }
            m.scripts[sj.at("unit").get<unsigned int>()] = std::move(script);
        }
        return m;
    }

    // ---- players ----------------------------------------------------

    json saveGamePlayerInfo(const GamePlayerInfo& p)
    {
        return json{
            {"name", saveOptional(p.name, [](const std::string& s) { return json(s); })},
            {"type", saveEnum(p.type)},
            {"color", p.color.value},
            {"status", saveEnum(p.status)},
            {"side", p.side},
            {"teamId", saveOptional(p.teamId, [](int v) { return json(v); })},
            {"metal", saveMetal(p.metal)},
            {"energy", saveEnergy(p.energy)},
            {"maxMetal", saveMetal(p.maxMetal)},
            {"maxEnergy", saveEnergy(p.maxEnergy)},
            {"startingMetal", saveMetal(p.startingMetal)},
            {"startingEnergy", saveEnergy(p.startingEnergy)},
            {"hasBaseStorage", p.hasBaseStorage},
            {"hasCreatedUnits", p.hasCreatedUnits},
            {"defeatCountdown", p.defeatCountdown},
            {"commanderType", p.commanderType},
            {"respawnCount", p.respawnCount},
            {"removedFromGame", p.removedFromGame},
            {"removeUnitsAt", saveOptional(p.removeUnitsAt, [](GameTime v) { return saveGameTime(v); })},
            {"metalStalled", p.metalStalled},
            {"energyStalled", p.energyStalled},
            // The original does not save these (TOTALA-EXE-COMMANDS.md
            // section 124); RWE does, because they steer the simulation
            // and a lockstep resume or replay keyframe without them would
            // diverge from the game it came from.
            {"shareMetal", p.shareMetal},
            {"shareEnergy", p.shareEnergy},
            {"metalReserve", saveMetal(p.metalReserve)},
            {"energyReserve", saveEnergy(p.energyReserve)},
            {"metalReservePercent", p.metalReservePercent},
            {"energyReservePercent", p.energyReservePercent},
            {"metalReservePercentage", p.metalReservePercentage},
            {"energyReservePercentage", p.energyReservePercentage},
            // TADR 2026.10.6: the Community Patch patrol reclaim levels.
            // Saved because they change what a patrol does; a save from
            // before they existed loads them at their default 20.
            {"reclaimMetalPercent", p.reclaimMetalPercent},
            {"reclaimEnergyPercent", p.reclaimEnergyPercent},
            {"unitsKilled", p.unitsKilled},
            {"unitsLost", p.unitsLost},
            {"metalProduced", saveMetal(p.metalProduced)},
            {"energyProduced", saveEnergy(p.energyProduced)},
            {"metalExcess", saveMetal(p.metalExcess)},
            {"energyExcess", saveEnergy(p.energyExcess)},
            {"desiredMetalConsumptionBuffer", saveMetal(p.desiredMetalConsumptionBuffer)},
            {"desiredEnergyConsumptionBuffer", saveEnergy(p.desiredEnergyConsumptionBuffer)},
            {"previousDesiredMetalConsumptionBuffer", saveMetal(p.previousDesiredMetalConsumptionBuffer)},
            {"previousDesiredEnergyConsumptionBuffer", saveEnergy(p.previousDesiredEnergyConsumptionBuffer)},
            {"metalProductionBuffer", saveMetal(p.metalProductionBuffer)},
            {"energyProductionBuffer", saveEnergy(p.energyProductionBuffer)},
            {"previousMetalProductionBuffer", saveMetal(p.previousMetalProductionBuffer)},
            {"previousEnergyProductionBuffer", saveEnergy(p.previousEnergyProductionBuffer)},
            {"metalRequestBuffer", saveMetal(p.metalRequestBuffer)},
            {"energyRequestBuffer", saveEnergy(p.energyRequestBuffer)},
            {"metalDebt", saveMetal(p.metalDebt)},
            {"energyDebt", saveEnergy(p.energyDebt)},
            // S:130: the schedule of the air base list rebuild and its draw.
            {"airBasesRebuiltAt", saveOptional(p.airBasesRebuiltAt, [](GameTime t) { return saveGameTime(t); })}};
    }

    GamePlayerInfo loadGamePlayerInfo(const json& j)
    {
        GamePlayerInfo p{
            loadOptional(j.at("name"), [](const json& v) { return v.get<std::string>(); }),
            loadEnum<GamePlayerType>(j.at("type")),
            PlayerColorIndex(j.at("color").get<unsigned int>()),
            loadEnum<GamePlayerStatus>(j.at("status")),
            j.at("side").get<std::string>(),
            loadMetal(j.at("metal")),
            loadEnergy(j.at("energy")),
            loadMetal(j.at("maxMetal")),
            loadEnergy(j.at("maxEnergy")),
            loadMetal(j.at("startingMetal")),
            loadEnergy(j.at("startingEnergy")),
            j.contains("teamId") ? loadOptional(j.at("teamId"), [](const json& v) { return v.get<int>(); }) : std::optional<int>()};
        // Added with missions (#293); every earlier save is a skirmish.
        p.hasBaseStorage = j.contains("hasBaseStorage") && j.at("hasBaseStorage").get<bool>();
        // Added with BT-193; an older save's players have all been given units.
        p.hasCreatedUnits = !j.contains("hasCreatedUnits") || j.at("hasCreatedUnits").get<bool>();
        p.defeatCountdown = j.value("defeatCountdown", -1);
        p.commanderType = j.value("commanderType", std::string());
        p.respawnCount = j.value("respawnCount", 0u);
        p.removedFromGame = j.value("removedFromGame", false);
        if (j.contains("removeUnitsAt"))
        {
            p.removeUnitsAt = loadOptional(j.at("removeUnitsAt"), [](const json& v) { return loadGameTime(v); });
        }
        p.metalStalled = j.at("metalStalled").get<bool>();
        p.energyStalled = j.at("energyStalled").get<bool>();
        // Resource sharing came later; an older save has it off, with no
        // reserve, which is what a game that never used it looks like.
        if (j.contains("shareMetal"))
        {
            p.shareMetal = j.at("shareMetal").get<bool>();
        }
        if (j.contains("shareEnergy"))
        {
            p.shareEnergy = j.at("shareEnergy").get<bool>();
        }
        if (j.contains("metalReserve"))
        {
            p.metalReserve = loadMetal(j.at("metalReserve"));
        }
        if (j.contains("energyReserve"))
        {
            p.energyReserve = loadEnergy(j.at("energyReserve"));
        }
        if (j.contains("metalReservePercent"))
        {
            p.metalReservePercent = j.at("metalReservePercent").get<bool>();
        }
        if (j.contains("energyReservePercent"))
        {
            p.energyReservePercent = j.at("energyReservePercent").get<bool>();
        }
        if (j.contains("metalReservePercentage"))
        {
            p.metalReservePercentage = j.at("metalReservePercentage").get<unsigned int>();
        }
        if (j.contains("energyReservePercentage"))
        {
            p.energyReservePercentage = j.at("energyReservePercentage").get<unsigned int>();
        }
        // Added with the Community Patch patrol thresholds (TADR
        // 2026.10.6). An older save predates them and loads the default.
        p.reclaimMetalPercent = std::clamp(j.value("reclaimMetalPercent", 20), 0, 100);
        p.reclaimEnergyPercent = std::clamp(j.value("reclaimEnergyPercent", 20), 0, 100);
        p.unitsKilled = j.at("unitsKilled").get<unsigned int>();
        p.unitsLost = j.at("unitsLost").get<unsigned int>();
        // Added after the first saves were written; an older file simply
        // has an empty chart.
        if (j.contains("metalProduced"))
        {
            p.metalProduced = loadMetal(j.at("metalProduced"));
            p.energyProduced = loadEnergy(j.at("energyProduced"));
            p.metalExcess = loadMetal(j.at("metalExcess"));
            p.energyExcess = loadEnergy(j.at("energyExcess"));
        }
        p.desiredMetalConsumptionBuffer = loadMetal(j.at("desiredMetalConsumptionBuffer"));
        p.desiredEnergyConsumptionBuffer = loadEnergy(j.at("desiredEnergyConsumptionBuffer"));
        p.previousDesiredMetalConsumptionBuffer = loadMetal(j.at("previousDesiredMetalConsumptionBuffer"));
        p.previousDesiredEnergyConsumptionBuffer = loadEnergy(j.at("previousDesiredEnergyConsumptionBuffer"));
        p.metalProductionBuffer = loadMetal(j.at("metalProductionBuffer"));
        p.energyProductionBuffer = loadEnergy(j.at("energyProductionBuffer"));
        p.previousMetalProductionBuffer = loadMetal(j.at("previousMetalProductionBuffer"));
        p.previousEnergyProductionBuffer = loadEnergy(j.at("previousEnergyProductionBuffer"));
        p.metalRequestBuffer = loadMetal(j.at("metalRequestBuffer"));
        p.energyRequestBuffer = loadEnergy(j.at("energyRequestBuffer"));
        p.metalDebt = loadMetal(j.at("metalDebt"));
        p.energyDebt = loadEnergy(j.at("energyDebt"));
        // Added with the air base query's schedule (S:130); an older
        // save has never rebuilt.
        if (j.contains("airBasesRebuiltAt"))
        {
            p.airBasesRebuiltAt = loadOptional(j.at("airBasesRebuiltAt"), [](const json& v) { return loadGameTime(v); });
        }
        return p;
    }

    // ---- features ---------------------------------------------------

    json saveMapFeature(const MapFeature& f)
    {
        return json{
            {"featureName", f.featureName.value},
            {"position", saveSimVector(f.position)},
            {"rotation", saveSimAngle(f.rotation)},
            {"velocity", saveSimVector(f.velocity)},
            {"reclaimProgress", f.reclaimProgress},
            {"hitPoints", f.hitPoints},
            {"burningUntil", saveOptional(f.burningUntil, [](GameTime t) { return saveGameTime(t); })},
            {"nextSpark", saveGameTime(f.nextSpark)},
            {"animation", static_cast<unsigned int>(f.animation)},
            {"animationTicksRemaining", f.animationTicksRemaining},
            {"burnSparkTicksRemaining", f.burnSparkTicksRemaining},
            {"animationOrder", f.animationOrder}};
    }
}
