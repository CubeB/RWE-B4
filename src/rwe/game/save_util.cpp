#include "save_util.h"

#include "SaveJson.h"
#include "UnitStateFieldTable.h"

// This one uses GameSimulation itself rather than only naming it, and
// save_util.h no longer drags the header in, so it says so here.
#include <rwe/sim/GameSimulation.h>

#include <algorithm>
#include <cstring>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MissionRules.h>
#include <rwe/sim/MissionScripts.h>
#include <rwe/util/match.h>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace rwe
{
    namespace
    {
        using nlohmann::json;

        // The json primitives and the id-remapping tables live in
        // SaveJson.h, where the unit field table can reach them too.

        // ---- id table layout --------------------------------------------
        //
        // See VectorMap::Layout. Written as one flat array per table,
        // [id, occupied, nextFree] a slot, because a map has thousands of
        // features and the keyframes hold dozens of these in memory.

        template <typename Layout>
        json saveLayout(const Layout& layout)
        {
            json slots = json::array();
            for (const auto& s : layout.slots)
            {
                slots.push_back(json::array({s.id, s.occupied, s.nextFreeIndex ? json(*s.nextFreeIndex) : json()}));
            }
            return json{
                {"firstFree", layout.firstFreeIndex ? json(*layout.firstFreeIndex) : json()},
                {"slots", slots},
            };
        }

        template <typename T, typename Tag>
        typename VectorMap<T, Tag>::Layout loadLayout(const json& j)
        {
            typename VectorMap<T, Tag>::Layout layout;
            const auto& firstFree = j.at("firstFree");
            layout.firstFreeIndex = firstFree.is_null() ? std::nullopt : std::make_optional(firstFree.get<unsigned int>());
            for (const auto& s : j.at("slots"))
            {
                const auto& next = s.at(2);
                layout.slots.push_back({s.at(0).get<unsigned int>(), s.at(1).get<bool>(), next.is_null() ? std::nullopt : std::make_optional(next.get<unsigned int>())});
            }
            return layout;
        }

        /** The slot numbers a layout's members occupy, in iteration order -- which is saved order. */
        template <typename Layout>
        std::vector<unsigned int> occupiedSlots(const Layout& layout)
        {
            std::vector<unsigned int> slots;
            for (unsigned int i = 0; i < layout.slots.size(); ++i)
            {
                if (layout.slots[i].occupied)
                {
                    slots.push_back(i);
                }
            }
            return slots;
        }

        json saveWinStatus(const WinStatus& s)
        {
            return match(
                s,
                [](const WinStatusWon& w) { return json{{"kind", "won"}, {"winner", w.winner.value}}; },
                [](const WinStatusDraw&) { return json{{"kind", "draw"}}; },
                [](const WinStatusUndecided&) { return json{{"kind", "undecided"}}; });
        }

        WinStatus loadWinStatus(const json& j)
        {
            const auto& kind = j.at("kind").get_ref<const std::string&>();
            if (kind == "won")
            {
                return WinStatusWon{PlayerId(j.at("winner").get<unsigned int>())};
            }
            if (kind == "draw")
            {
                return WinStatusDraw();
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
                {"metalStalled", p.metalStalled},
                {"energyStalled", p.energyStalled},
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
                {"energyDebt", saveEnergy(p.energyDebt)}};
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
            p.metalStalled = j.at("metalStalled").get<bool>();
            p.energyStalled = j.at("energyStalled").get<bool>();
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
                {"nextSpark", saveGameTime(f.nextSpark)}};
        }
    }

    /**
     * The per-type save and load helpers the field table points at live in
     * save_util_unit.cpp and save_util_orders.cpp. They were moved out to
     * keep this object under the COFF section ceiling; save_util.h is where
     * they are declared.
     */
    namespace
    {
        using nlohmann::json;

        /**
         * The save walk: one lookup into the field table instead of a
         * hand-written member list. The table is the single declaration, so
         * a new field is written once there and in no other file.
         */
        json saveUnitState(const UnitState& u, const SaveContext& ctx)
        {
            json j = json::object();
            for (const auto& field : unitStateFieldTable())
            {
                j[field.name] = std::visit([&](const auto& w) { return w.save(u, ctx); }, field.walks);
            }
            return j;
        }

        /**
         * Fills a unit that was emplaced with its pieces and a fresh
         * CobEnvironment. The load walk reads the field table in declared
         * order, and the COB environment's row sits before the weapons row,
         * which is what weapon aim state needs when it resolves its thread
         * reference against the restored threads.
         */
        void loadUnitStateInto(const json& j, UnitState& u, const LoadContext& ctx)
        {
            for (const auto& field : unitStateFieldTable())
            {
                if (std::holds_alternative<UnitStateFieldSaveOnly>(field.walks))
                {
                    continue;
                }
                const json value = (field.mayBeMissing && !j.contains(field.name))
                    ? json()
                    : j.at(field.name);
                std::visit(
                    [&](const auto& w)
                    {
                        using T = std::decay_t<decltype(w)>;
                        if constexpr (!std::is_same_v<T, UnitStateFieldSaveOnly>)
                        {
                            w.load(value, u, ctx);
                        }
                    },
                    field.walks);
            }
        }

        // ---- projectiles ------------------------------------------------

        json saveProjectile(const Projectile& p, const SaveContext& ctx)
        {
            json damage = json::object();
            for (const auto& [unitType, amount] : p.damage)
            {
                damage[unitType] = amount;
            }

            return json{
                {"weaponType", p.weaponType},
                {"owner", p.owner.value},
                {"attacker", saveOptional(p.attacker, [&](UnitId id) { return saveUnitIdRef(id, ctx); })},
                {"position", saveSimVector(p.position)},
                {"previousPosition", saveSimVector(p.previousPosition)},
                {"origin", saveSimVector(p.origin)},
                {"velocity", saveSimVector(p.velocity)},
                {"lastSmoke", saveGameTime(p.lastSmoke)},
                {"damage", damage},
                {"dieOnFrame", saveOptional(p.dieOnFrame, [](GameTime t) { return saveGameTime(t); })},
                {"damageRadius", saveSimScalar(p.damageRadius)},
                {"edgeEffectiveness", saveSimScalar(p.edgeEffectiveness)},
                {"groundBounce", p.groundBounce},
                {"isDead", p.isDead},
                {"createdAt", saveGameTime(p.createdAt)},
                {"targetUnit", saveOptional(p.targetUnit, [&](UnitId id) { return saveUnitIdRef(id, ctx); })},
                {"targetProjectile", saveOptional(p.targetProjectile, [&](ProjectileId id) { return saveProjectileIdRef(id, ctx); })},
                {"targetPosition", saveOptional(p.targetPosition, saveSimVector)},
                {"heading", saveSimAngle(p.heading)},
                {"pitch", saveSimAngle(p.pitch)},
                {"speed", saveSimScalar(p.speed)},
                {"motorOutFrame", saveOptional(p.motorOutFrame, [](GameTime t) { return saveGameTime(t); })},
                {"secondPhase", p.secondPhase},
                {"motorOut", p.motorOut}};
        }

        void loadProjectileInto(const json& j, Projectile& p, const LoadContext& ctx)
        {
            p.weaponType = j.at("weaponType").get<std::string>();
            p.owner = PlayerId(j.at("owner").get<unsigned int>());
            p.attacker = loadOptional(j.at("attacker"), [&](const json& v) { return loadUnitIdRef(v, ctx); });
            p.position = loadSimVector(j.at("position"));
            p.previousPosition = loadSimVector(j.at("previousPosition"));
            p.origin = loadSimVector(j.at("origin"));
            p.velocity = loadSimVector(j.at("velocity"));
            p.lastSmoke = loadGameTime(j.at("lastSmoke"));
            p.damage.clear();
            for (const auto& [unitType, amount] : j.at("damage").items())
            {
                p.damage[unitType] = amount.get<unsigned int>();
            }
            p.dieOnFrame = loadOptional(j.at("dieOnFrame"), loadGameTime);
            p.damageRadius = loadSimScalar(j.at("damageRadius"));
            p.edgeEffectiveness = loadSimScalar(j.at("edgeEffectiveness"));
            p.groundBounce = j.at("groundBounce").get<bool>();
            p.isDead = j.at("isDead").get<bool>();
            p.createdAt = loadGameTime(j.at("createdAt"));
            p.targetUnit = loadOptional(j.at("targetUnit"), [&](const json& v) { return loadUnitIdRef(v, ctx); });
            p.targetProjectile = loadOptional(j.at("targetProjectile"), [&](const json& v) { return loadProjectileIdRef(v, ctx); });
            p.targetPosition = loadOptional(j.at("targetPosition"), loadSimVector);
            p.heading = loadSimAngle(j.at("heading"));
            p.pitch = loadSimAngle(j.at("pitch"));
            p.speed = loadSimScalar(j.at("speed"));
            p.motorOutFrame = loadOptional(j.at("motorOutFrame"), loadGameTime);
            p.secondPhase = j.at("secondPhase").get<bool>();
            p.motorOut = j.at("motorOut").get<bool>();
        }

        /**
         * Puts a loaded unit back onto the map the way tryAddUnit and the
         * air transitions would have left it: a carried unit holds no ground,
         * a flying unit is in the flying set, a ground unit stamps its
         * footprint, a building stamps its yardmap.
         */
        void restoreUnitOccupancy(GameSimulation& sim, UnitId unitId, const UnitState& unit)
        {
            if (unit.carriedBy)
            {
                return;
            }

            const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);

            if (unitDefinition.isMobile && isFlying(unit.physics))
            {
                sim.flyingUnitsSet.insert(unitId);
                return;
            }

            auto footprintRect = sim.computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
            auto footprintRegion = sim.occupiedGrid.tryToRegion(footprintRect);
            if (!footprintRegion)
            {
                throw std::runtime_error("loaded unit's footprint does not fit the map");
            }

            if (unitDefinition.isMobile)
            {
                sim.occupiedGrid.forEach(*footprintRegion, [unitId](auto& cell) { cell.mobileUnitId = unitId; });
            }
            else
            {
                if (!unitDefinition.yardMap)
                {
                    throw std::runtime_error("loaded building has no yardmap in its definition");
                }
                sim.occupiedGrid.forEach2(footprintRegion->x, footprintRegion->y, *unitDefinition.yardMap, [&](auto& cell, const auto& yardMapCell) {
                    cell.buildingInfo = OccupiedCellBuildingInfo{unitId, isPassable(yardMapCell, unit.yardOpen)};
                });
            }
        }
    }

    nlohmann::json saveExploredGrid(const Grid<ExploredMask>& grid)
    {
        // Runs of equal cell values rather than a bit per position: the grid
        // is almost all zeros with a few explored blobs, so a long-game map
        // comes out as a handful of pairs rather than tens of thousands of
        // numbers. The value is written out rather than implied, because with
        // one bit per group a cell is a small mask and not just 0 or 1.
        auto runs = json::array();
        const auto& cells = grid.getVector();
        std::size_t i = 0;
        while (i < cells.size())
        {
            auto mask = cells[i];
            std::size_t j = i;
            while (j < cells.size() && cells[j] == mask)
            {
                ++j;
            }
            runs.push_back(json::array({mask, static_cast<std::uint32_t>(j - i)}));
            i = j;
        }

        return json{
            {"width", grid.getWidth()},
            {"height", grid.getHeight()},
            {"runs", runs}};
    }

    void loadExploredGrid(const nlohmann::json& j, Grid<ExploredMask>& grid)
    {
        // A save from a different map, or a different vision cell size,
        // cannot be laid over this grid. Leaving it unexplored is the safe
        // answer: the player loses their map memory rather than the load
        // failing outright.
        auto width = j.at("width").get<std::size_t>();
        auto height = j.at("height").get<std::size_t>();
        if (width != static_cast<std::size_t>(grid.getWidth()) || height != static_cast<std::size_t>(grid.getHeight()))
        {
            return;
        }

        auto& cells = grid.getVector();
        std::fill(cells.begin(), cells.end(), static_cast<ExploredMask>(0));

        std::size_t index = 0;
        for (const auto& runJson : j.at("runs"))
        {
            auto mask = runJson.at(0).get<ExploredMask>();
            auto run = runJson.at(1).get<std::uint32_t>();

            // The bound is on the cell being written, not on the start of the
            // run: guarding `index` alone lets a run that begins in range
            // finish past the end of the vector.
            for (std::uint32_t i = 0; i < run && index < cells.size(); ++i)
            {
                cells[index] = mask;
                ++index;
            }
            if (index >= cells.size())
            {
                break;
            }
        }
    }

    nlohmann::json saveSimulationToJson(const GameSimulation& sim)
    {
        SaveContext ctx;
        {
            uint32_t i = 0;
            for (const auto& [id, _] : sim.units)
            {
                ctx.units.insert({id, i++});
            }
        }
        {
            uint32_t i = 0;
            for (const auto& [id, _] : sim.features)
            {
                ctx.features.insert({FeatureId(id.value), i++});
            }
        }
        {
            uint32_t i = 0;
            for (const auto& [id, _] : sim.projectiles)
            {
                ctx.projectiles.insert({id, i++});
            }
        }

        json j;
        // 2 since 2026-09-11. The layout did not change; what changed is
        // that a version 1 save may hold units made before mobile units were
        // shaded, which loadSimulationFromJson has to put right.
        j["version"] = 2;

        // The wind range is a construction-time constant; it travels with the
        // save only so the load can check it was given the right map.
        j["minWindSpeed"] = sim.minWindSpeed;
        j["maxWindSpeed"] = sim.maxWindSpeed;

        j["gameTime"] = saveGameTime(sim.gameTime);

        {
            std::ostringstream ss;
            ss << sim.rng;
            j["rng"] = ss.str();
        }

        j["gameStatus"] = saveWinStatus(sim.gameStatus);
        j["currentWindGenerationFactor"] = saveSimScalar(sim.currentWindGenerationFactor);
        j["currentWindVector"] = saveSimVector(sim.currentWindVector);
        j["tidalStrength"] = sim.tidalStrength;
        j["killMul"] = sim.killMul;
        j["timeMul"] = sim.timeMul;
        j["nextWindSpeedChange"] = saveGameTime(sim.nextWindSpeedChange);
        j["featureRegrowthCursor"] = sim.featureRegrowthCursor;
        if (sim.missionRules)
        {
            j["missionRules"] = saveMissionRules(*sim.missionRules);
        }
        if (sim.missionScripts)
        {
            j["missionScripts"] = saveMissionScripts(*sim.missionScripts);
        }

        json players = json::array();
        for (const auto& p : sim.players)
        {
            players.push_back(saveGamePlayerInfo(p));
        }
        j["players"] = players;

        // The one explored grid, a bit per line-of-sight group. Saved whole
        // and once, because it belongs to the game rather than to any player;
        // the visible grid is not saved at all, since the load recomputes it
        // from where the units are.
        j["explored"] = saveExploredGrid(sim.explored);

        json features = json::array();
        for (const auto& [_, f] : sim.features)
        {
            features.push_back(saveMapFeature(f));
        }
        j["features"] = features;

        json units = json::array();
        for (const auto& [_, u] : sim.units)
        {
            units.push_back(saveUnitState(u, ctx));
        }
        j["units"] = units;

        json projectiles = json::array();
        for (const auto& [_, p] : sim.projectiles)
        {
            projectiles.push_back(saveProjectile(p, ctx));
        }
        j["projectiles"] = projectiles;

        // The shape of the three id tables, so the load can put everything
        // back in the slot it came from. Without this the load hands out
        // dense ids, and the game is the same game only until the next unit
        // is born: VectorMap refills freed slots, so a fresh table puts the
        // newcomer at the end where the original put it in a hole, and from
        // there on the two iterate in different orders and hash differently.
        // A keyframe the replay viewer winds forward from cannot afford
        // that; a saved game merely deserved better.
        j["layout"] = json{
            {"features", saveLayout(sim.features.layout())},
            {"units", saveLayout(sim.units.layout())},
            {"projectiles", saveLayout(sim.projectiles.layout())},
        };

        json pathRequests = json::array();
        for (const auto& r : sim.pathRequests)
        {
            pathRequests.push_back(saveUnitIdRef(r.unitId, ctx));
        }
        j["pathRequests"] = pathRequests;

        // A path search part way through, if there is one. It belongs to the
        // request at the head of the queue above, so it needs no name of its
        // own -- only the footprint it started from, because the unit has
        // been walking its straight-line stand-in since and is no longer
        // standing there, and how far it had got. See
        // PathFindingService::suspendedSearchStart for why that is enough and
        // why dropping the search instead would break a replay.
        if (auto searchStart = sim.pathFindingService.suspendedSearchStart())
        {
            j["pathSearch"] = json{
                {"start", saveDiscreteRect(*searchStart)},
                {"expansions", sim.pathFindingService.suspendedSearchExpansions()}};
        }

        // Empty between ticks (spawnNewUnits drains it), but carried along so
        // a mid-tick snapshot would not lose anything.
        json unitCreationRequests = json::array();
        for (const auto& id : sim.unitCreationRequests)
        {
            unitCreationRequests.push_back(saveUnitIdRef(id, ctx));
        }
        j["unitCreationRequests"] = unitCreationRequests;

        return j;
    }

    void loadSimulationFromJson(const nlohmann::json& j, GameSimulation& sim)
    {
        auto version = j.at("version").get<int>();
        if (version != 1 && version != 2)
        {
            throw std::runtime_error("unsupported save version");
        }
        if (j.at("minWindSpeed").get<int>() != sim.minWindSpeed || j.at("maxWindSpeed").get<int>() != sim.maxWindSpeed)
        {
            throw std::runtime_error("save was made for a map with a different wind range");
        }
        if (!sim.players.empty())
        {
            throw std::runtime_error("loadSimulationFromJson needs a simulation with no players added");
        }
        {
            auto it = sim.units.begin();
            if (it != sim.units.end())
            {
                throw std::runtime_error("loadSimulationFromJson needs a simulation with no units spawned");
            }
        }

        for (const auto& pj : j.at("players"))
        {
            sim.addPlayer(loadGamePlayerInfo(pj));
        }

        sim.gameTime = loadGameTime(j.at("gameTime"));
        {
            std::istringstream ss(j.at("rng").get<std::string>());
            ss >> sim.rng;
        }
        sim.gameStatus = loadWinStatus(j.at("gameStatus"));
        sim.currentWindGenerationFactor = loadSimScalar(j.at("currentWindGenerationFactor"));
        if (j.contains("currentWindVector"))
        {
            // Absent from saves written before the wind was ported. Leaving
            // those at zero is harmless -- the next wind change rebuilds it
            // within fourteen seconds.
            sim.currentWindVector = loadSimVector(j.at("currentWindVector"));
        }
        sim.tidalStrength = j.at("tidalStrength").get<int>();
        if (j.contains("killMul"))
        {
            sim.killMul = j.at("killMul").get<int>();
            sim.timeMul = j.at("timeMul").get<int>();
        }
        sim.nextWindSpeedChange = loadGameTime(j.at("nextWindSpeedChange"));
        sim.featureRegrowthCursor = j.at("featureRegrowthCursor").get<int>();
        // Only a mission's save has them, and they are the whole of what the
        // mission's rules have seen: the counts left, what has latched and
        // the countdown.
        if (j.contains("missionRules"))
        {
            sim.missionRules = std::make_unique<MissionRules>(loadMissionRules(j.at("missionRules"), sim.players.size()));
        }
        else
        {
            sim.missionRules.reset();
        }
        if (j.contains("missionScripts"))
        {
            sim.missionScripts = std::make_unique<MissionScripts>(loadMissionScripts(j.at("missionScripts")));
        }
        else
        {
            sim.missionScripts.reset();
        }

        // The fresh sim has the map's initial features standing. Sweep them
        // all away -- grid cells included -- and put the saved ones down in
        // saved order, so the loaded feature array iterates the way the saved
        // one did. Replacing the whole VectorMap resets its free list, which
        // is what makes the handed-out ids dense again. The metal and geo
        // grids are left alone: only permanent features write them, permanent
        // features are never removed, so the saved set puts back exactly what
        // the initial set wrote.
        sim.occupiedGrid.forEachIndexed([](const auto&, OccupiedCell& cell) { cell.featureId = std::nullopt; });
        sim.features = VectorMap<MapFeature, FeatureIdTag>();

        // A save that recorded the shape of its id tables is put back into
        // that shape, member by member into the slot each came from. One
        // without (any save older than the layout) is laid out densely, as
        // it always was, and the ids it hands out are the load's own.
        std::optional<std::vector<unsigned int>> featureSlots;
        std::optional<std::vector<unsigned int>> unitSlots;
        std::optional<std::vector<unsigned int>> projectileSlots;
        if (j.contains("layout"))
        {
            const auto& lj = j.at("layout");
            auto featureLayout = loadLayout<MapFeature, FeatureIdTag>(lj.at("features"));
            auto unitLayout = loadLayout<UnitState, UnitIdTag>(lj.at("units"));
            auto projectileLayout = loadLayout<Projectile, ProjectileIdTag>(lj.at("projectiles"));
            featureSlots = occupiedSlots(featureLayout);
            unitSlots = occupiedSlots(unitLayout);
            projectileSlots = occupiedSlots(projectileLayout);
            if (featureSlots->size() != j.at("features").size()
                || unitSlots->size() != j.at("units").size()
                || projectileSlots->size() != j.at("projectiles").size())
            {
                throw std::runtime_error("save layout does not match its contents");
            }
            sim.features.restoreLayout(featureLayout);
            sim.units.restoreLayout(unitLayout);
            sim.projectiles.restoreLayout(projectileLayout);
        }

        LoadContext ctx;

        for (const auto& fj : j.at("features"))
        {
            MapFeature f;
            f.featureName = FeatureDefinitionId(fj.at("featureName").get<unsigned int>());
            f.position = loadSimVector(fj.at("position"));
            f.rotation = loadSimAngle(fj.at("rotation"));
            std::optional<FeatureId> id;
            if (featureSlots)
            {
                id = sim.addFeatureInSlot((*featureSlots)[ctx.features.size()], std::move(f));
            }
            else
            {
                id = sim.addFeature(std::move(f));
            }
            if (!id)
            {
                throw std::runtime_error("could not place a loaded feature");
            }
            auto& placed = sim.features.tryGet(*id)->get();
            // Older saves predate sinking wreckage; theirs simply sit still.
            placed.velocity = fj.contains("velocity") ? loadSimVector(fj.at("velocity")) : SimVector(0_ss, 0_ss, 0_ss);
            placed.reclaimProgress = fj.at("reclaimProgress").get<unsigned int>();
            placed.hitPoints = fj.at("hitPoints").get<unsigned int>();
            placed.burningUntil = loadOptional(fj.at("burningUntil"), loadGameTime);
            placed.nextSpark = loadGameTime(fj.at("nextSpark"));
            ctx.features.push_back(*id);
        }

        // Units and projectiles go in two passes: first every slot is claimed
        // so the id tables are complete, then the state is filled in, so a
        // reference can point forwards as well as backwards.
        const auto& unitsJson = j.at("units");
        for (const auto& uj : unitsJson)
        {
            auto unitType = uj.at("unitType").get<std::string>();
            auto scriptIt = sim.unitScriptDefinitions.find(unitType);
            if (scriptIt == sim.unitScriptDefinitions.end())
            {
                throw std::runtime_error("no script loaded for unit type " + unitType);
            }

            std::vector<UnitMesh> pieces;
            for (const auto& pj : uj.at("pieces"))
            {
                pieces.push_back(loadUnitMesh(pj));
            }

            // Until 2026-09-06 createUnit cleared the shade flag on every
            // piece of anything mobile, and a save keeps the flag, so a unit
            // made back then has come through every save and load since with
            // no piece shaded -- a commander from the first minute of a long
            // game, for one. In a version 1 save a mobile unit with no piece
            // shaded is taken to be one of those and shaded again. A script
            // can ask for that too, by saying DONT_SHADE on every piece, and
            // an old save gets that one case wrong; nothing written since
            // version 2 is touched.
            if (version == 1)
            {
                auto definitionIt = sim.unitDefinitions.find(unitType);
                auto noneShaded = std::none_of(pieces.begin(), pieces.end(), [](const UnitMesh& m) { return m.shaded; });
                if (definitionIt != sim.unitDefinitions.end() && definitionIt->second.isMobile && !pieces.empty() && noneShaded)
                {
                    for (auto& m : pieces)
                    {
                        m.shaded = true;
                    }
                }
            }

            auto env = std::make_unique<CobEnvironment>(&scriptIt->second);
            if (unitSlots)
            {
                ctx.units.push_back(UnitId(sim.units.emplaceInSlot((*unitSlots)[ctx.units.size()], pieces, std::move(env))));
            }
            else
            {
                ctx.units.push_back(UnitId(sim.units.emplace(pieces, std::move(env))));
            }
        }

        const auto& projectilesJson = j.at("projectiles");
        for (std::size_t i = 0; i < projectilesJson.size(); ++i)
        {
            if (projectileSlots)
            {
                ctx.projectiles.push_back(ProjectileId(sim.projectiles.emplaceInSlot((*projectileSlots)[i])));
            }
            else
            {
                ctx.projectiles.push_back(ProjectileId(sim.projectiles.emplace()));
            }
        }

        {
            std::size_t i = 0;
            for (const auto& uj : unitsJson)
            {
                loadUnitStateInto(uj, sim.units.tryGet(ctx.units[i])->get(), ctx);
                ++i;
            }
        }

        {
            std::size_t i = 0;
            for (const auto& pj : projectilesJson)
            {
                loadProjectileInto(pj, sim.projectiles.tryGet(ctx.projectiles[i])->get(), ctx);
                ++i;
            }
        }

        for (const auto& [id, unit] : sim.units)
        {
            restoreUnitOccupancy(sim, id, unit);
        }

        sim.pathFindingService.abandonSearch();
        sim.pathRequests.clear();
        for (const auto& rj : j.at("pathRequests"))
        {
            sim.pathRequests.push_back(PathRequest{loadUnitIdRef(rj, ctx)});
        }

        sim.unitCreationRequests.clear();
        for (const auto& rj : j.at("unitCreationRequests"))
        {
            sim.unitCreationRequests.push_back(loadUnitIdRef(rj, ctx));
        }

        sim.events.clear();

        sim.updateVisibility();

        // Explored last, and overwriting rather than adding to what the pass
        // above just revealed. The grid is meant to be exactly what the player
        // had: anything a unit can currently see was already explored when the
        // save was written, in any state the engine actually produces, so the
        // pass can only ever agree with the file or disagree with it -- and
        // where they disagree the file is the one that knows where the player
        // has been. Doing it this way also makes save -> load -> save
        // byte-identical, which is what the round-trip test can check.
        //
        // Older saves carry no such key and simply start unexplored, as they
        // did before any of this was written.
        if (j.contains("explored"))
        {
            loadExploredGrid(j.at("explored"), sim.explored);
        }

        // Last of all, because rebuilding a suspended path search runs the
        // search, and the search reads the terrain, the occupancy grid and
        // the unit it belongs to -- all of which are only now in place.
        // Older saves carry no such key and simply start the search again,
        // as they did before any of this was written.
        if (j.contains("pathSearch"))
        {
            const auto& searchJson = j.at("pathSearch");
            sim.pathFindingService.restoreSuspendedSearch(
                sim,
                loadDiscreteRect(searchJson.at("start")),
                searchJson.at("expansions").get<std::size_t>());
        }
    }

    void clearSimulationForLoad(GameSimulation& sim)
    {
        sim.clearPlayers();

        // Whole tables replaced rather than emptied one member at a time:
        // the load puts its own layout in, so there is nothing to keep, and
        // going through the death paths would fire events and write wrecks.
        sim.units = VectorMap<UnitState, UnitIdTag>();
        sim.projectiles = VectorMap<Projectile, ProjectileIdTag>();
        sim.flyingUnitsSet.clear();
        sim.occupiedGrid.forEachIndexed([](const auto&, OccupiedCell& cell) {
            cell.mobileUnitId = std::nullopt;
            cell.buildingInfo = std::nullopt;
        });
        // The features are the load's to sweep, as they are for a fresh sim.

        // Derived caches that would otherwise answer for units that are no
        // longer there. The spatial index is stamped with the game time it
        // was built at and a restore can land on that very tick.
        sim.invalidateUnitSpatialIndex();
        sim.pathFindingService.abandonSearch();
        sim.pathRequests.clear();
        sim.unitCreationRequests.clear();
        sim.events.clear();
        sim.aiPendingCommands.clear();
    }
}
