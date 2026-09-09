// The debug harness, out of the scene proper.
//
// Two things live here that the game itself never reaches: the standing
// battle `battle_test` drives (enableBattleTest/runBattleTest) and the F10
// debug panel's unit spawner and placer. Both are dense in the way that
// costs a translation unit its section budget -- ImGui calls, strings,
// vectors and variants, every one of them a fresh COMDAT at -O0 -- and
// neither has anything to do with running a game. See CLAUDE.md on the COFF
// section ceiling for why that matters and why splitting off code that is
// merely *long* does not help.
#include "GameScene.h"
#include <algorithm>
#include <rwe/ImGuiContext.h>
#include <rwe/game/GameScene_util.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/match.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    void GameScene::enableBattleTest(unsigned int unitsPerSide, const std::vector<std::string>& unitTypes, const std::vector<PlayerId>& players, const std::vector<SimVector>& spawns)
    {
        // Every one of the checks below is otherwise a way to end up staring
        // at an empty field with nothing on screen to say why, so each of
        // them says what is wrong while there is still someone to read it.
        // The harness throws rather than carrying on: the caller turns an
        // exception into a message box and a line on stderr.
        if (players.size() < 2 || spawns.size() != players.size())
        {
            throw std::runtime_error("Battle test needs two sides with start positions, but this map and player list gave " + std::to_string(players.size()) + ". Pick a map with at least two start positions.");
        }
        if (unitTypes.empty())
        {
            throw std::runtime_error("Battle test was given no unit type to spawn");
        }

        battleTestUnitsPerSide = static_cast<int>(unitsPerSide);
        battleTestUnitTypes.clear();
        battleTestPlayers = players;
        battleTestSpawns = spawns;
        battleTestSpawnCounter.assign(players.size(), 0u);
        battleTestFootprint.clear();
        battleTestAlive.assign(players.size(), 0);

        for (std::size_t i = 0; i < players.size(); ++i)
        {
            // The definitions are keyed by the FBI's UnitName in upper case,
            // so accept --unit-type armah as readily as ARMAH.
            auto unitType = toUpper(unitTypes.at(i % unitTypes.size()));

            // A name the data does not define -- or one whose model or script
            // failed to load -- takes the game down on the first spawn, deep
            // inside a map::at, with nothing to say which name was wrong.
            auto definitionIt = simulation.unitDefinitions.find(unitType);
            if (definitionIt == simulation.unitDefinitions.end())
            {
                throw std::runtime_error("Battle test: the data defines no unit called '" + unitType + "'. Run with --list-units to see what it does define.");
            }
            if (simulation.unitModelDefinitions.find(definitionIt->second.objectName) == simulation.unitModelDefinitions.end())
            {
                throw std::runtime_error("Battle test: unit '" + unitType + "' names a model, " + definitionIt->second.objectName + ", that did not load");
            }
            if (simulation.unitScriptDefinitions.find(unitType) == simulation.unitScriptDefinitions.end())
            {
                throw std::runtime_error("Battle test: unit '" + unitType + "' has no script loaded");
            }

            // How big the unit is decides how far apart the spawn slots go;
            // runBattleTest works the spacing out per frame, since it also
            // depends on how many the slider is asking for.
            auto [footprintX, footprintZ] = simulation.getFootprintXZ(definitionIt->second.movementCollisionInfo);
            auto footprint = static_cast<int>(std::max(footprintX, footprintZ));
            battleTestFootprint.push_back(footprint);

            battleTestUnitTypes.push_back(unitType);

            LOG_INFO << "Battle test: player " << i << " fields " << unitType
                     << " (" << footprint << " tiles square)"
                     << " from " << simScalarToFloat(spawns[i].x) << ", " << simScalarToFloat(spawns[i].z);
        }

        LOG_INFO << "Battle test: " << battleTestUnitsPerSide << " units a side across " << players.size() << " players";

        // Start the heartbeat's clock here, or the first line reports every
        // millisecond since the process started as one frame.
        battleTestLastLogTime = sceneContext.timeService->getTicks();
        battleTestFramesSinceLog = 0;

        // The whole point is watching the fight, so the map is open from the
        // start rather than lit a unit at a time.
        fogOfWarEnabled = false;
    }

    void GameScene::runBattleTest()
    {
        if (battleTestPlayers.size() < 2 || battleTestSpawns.size() != battleTestPlayers.size())
        {
            return;
        }

        auto wanted = battleTestUnitsPerSide;

        // Every few seconds the harness looks again at anything that has come
        // to a stop, so this is also where the heartbeat lands.
        ++battleTestFramesSinceLog;
        auto now = sceneContext.timeService->getTicks();
        auto sweepDue = now - battleTestLastLogTime >= 5000;

        // Count what each player still has standing, keeping the ids as well
        // as the tally: the slider has to be able to take units away again,
        // not only put them there. The ones with nothing left to do are worth
        // knowing about separately -- see the sweep below.
        battleTestAlive.assign(battleTestPlayers.size(), 0);
        std::vector<std::vector<UnitId>> living(battleTestPlayers.size());
        std::vector<std::vector<UnitId>> idle(battleTestPlayers.size());
        for (const auto& [unitId, unit] : simulation.units)
        {
            if (unit.isDead())
            {
                continue;
            }
            for (std::size_t i = 0; i < battleTestPlayers.size(); ++i)
            {
                if (unit.owner == battleTestPlayers[i])
                {
                    ++battleTestAlive[i];
                    living[i].push_back(unitId);
                    if (sweepDue && unit.orders.empty())
                    {
                        idle[i].push_back(unitId);
                    }
                }
            }
        }

        for (std::size_t i = 0; i < battleTestPlayers.size(); ++i)
        {
            // A few at a time, in either direction: putting two hundred units
            // on the field in one tick stalls the frame and tells you nothing
            // about the fight, and deleting two hundred in one tick floods
            // the event queue for no better reason.
            auto budget = 8;

            // Dragging the slider down takes the newest away first. Those are
            // the ones still standing in the spawn block rather than fighting,
            // so the battle in the middle of the map is left alone. They go
            // quietly -- no wreck, no explosion -- because this is the harness
            // removing them, not the enemy killing them.
            while (battleTestAlive[i] > wanted && budget > 0 && !living[i].empty())
            {
                --budget;
                simulation.quietlyKillUnit(living[i].back());
                living[i].pop_back();
                --battleTestAlive[i];
                ++battleTestCulled;
            }

            const auto& home = battleTestSpawns[i];
            const auto& enemy = battleTestSpawns[(i + 1) % battleTestSpawns.size()];
            const auto& unitType = battleTestUnitTypes.at(i);
            const auto& definition = simulation.unitDefinitions.at(unitType);
            auto movementClass = simulation.getAdHocMovementClass(definition.movementCollisionInfo);
            auto footprint = battleTestFootprint.at(i);

            // Twice as many slots as units asked for, laid out square. The
            // slack matters: a slot blocked by a tree, or by a unit that has
            // not moved off yet, then costs a turn in the queue rather than a
            // place on the field. A block sized exactly to the count can
            // never fill.
            auto slots = std::max(4, wanted * 2);
            auto columns = 1;
            while (columns * columns < slots)
            {
                ++columns;
            }
            auto rows = (slots + columns - 1) / columns;

            const auto& terrain = simulation.terrain;
            auto tile = static_cast<int>(MapTerrain::HeightTileWidthInWorldUnits.value);
            auto mapWidth = static_cast<int>(simScalarToFloat(terrain.rightCutoffInWorldUnits() - terrain.leftInWorldUnits()));
            auto mapDepth = static_cast<int>(simScalarToFloat(terrain.bottomCutoffInWorldUnits() - terrain.topInWorldUnits()));

            // Lanes as wide as the unit itself wherever the map has room for
            // them, which means slots two footprints apart. Anything tighter
            // is a block nothing can leave: a three-tile Swatter cannot walk
            // down a one-tile lane, so the pathfinder finds no route off an
            // interior slot, gives up, and the middle of the block stands
            // there for the length of the run while only its edge feeds the
            // fight. Watched at a hundred a side, that was two thirds of the
            // army.
            //
            // Where the map has not got the room -- five hundred Swatters a
            // side want more ground than Coast To Coast has, twice over --
            // the block closes up rather than hanging off the edge, down to a
            // floor of one tile of clearance. That trades a mobile fight for
            // a full field, which at least is the count that was asked for,
            // and the heartbeat's alive figures say which of the two you got.
            auto pitch = std::min({footprint * 2 * tile, mapWidth / columns, mapDepth / rows});
            pitch = std::max(pitch, (footprint + 1) * tile);

            // Send the stopped ones off again. A unit whose route was blocked
            // when it was asked -- by the crowd it spawned in, or by ground
            // its movement class will not take -- drops the order and stands
            // there for the rest of the run, and a battle test with a growing
            // pool of statues in the corner is measuring the wrong thing. The
            // crowd is different a few seconds later, so asking again is
            // usually enough. Only for units still a long way from the enemy:
            // one that has arrived has honestly finished its order, and
            // re-issuing there would just churn.
            if (sweepDue)
            {
                auto farEnough = intToSimScalar(pitch * 4);
                for (auto unitId : idle[i])
                {
                    auto unit = tryGetUnit(unitId);
                    if (!unit)
                    {
                        continue;
                    }
                    if (unit->get().position.distanceSquared(enemy) < farEnough * farEnough)
                    {
                        continue;
                    }
                    unit->get().addOrder(MoveOrder(enemy));
                    ++battleTestReordered;
                }
            }

            // Keep the block on the map. Start positions sit near an edge as
            // often as not -- Coast To Coast puts one of its two within a
            // third of the map's width of the corner -- and a block centred
            // blindly on such a spawn hangs half of itself over the side,
            // where no unit can ever be placed. At five hundred a side that
            // alone left one player a quarter short for the whole run, every
            // attempt failing on a slot that was never on the map.
            auto halfWidth = intToSimScalar(columns * pitch) / 2_ss;
            auto halfDepth = intToSimScalar(rows * pitch) / 2_ss;
            auto centre = home;
            auto lowX = terrain.leftInWorldUnits() + halfWidth;
            auto highX = terrain.rightCutoffInWorldUnits() - halfWidth;
            centre.x = lowX < highX ? rweMax(lowX, rweMin(highX, home.x)) : (terrain.leftInWorldUnits() + terrain.rightCutoffInWorldUnits()) / 2_ss;
            auto lowZ = terrain.topInWorldUnits() + halfDepth;
            auto highZ = terrain.bottomCutoffInWorldUnits() - halfDepth;
            centre.z = lowZ < highZ ? rweMax(lowZ, rweMin(highZ, home.z)) : (terrain.topInWorldUnits() + terrain.bottomCutoffInWorldUnits()) / 2_ss;

            while (battleTestAlive[i] < wanted && budget > 0)
            {
                --budget;

                // The counter keeps climbing whether or not the last attempt
                // found room, so a blocked cell moves the next one along
                // instead of trying the same spot for ever -- which is what a
                // straight retry does, and it looks exactly like everything
                // spawning in one place.
                auto slot = static_cast<int>(battleTestSpawnCounter[i]++ % static_cast<unsigned int>(columns * rows));
                auto column = (slot % columns) - (columns / 2);
                auto rank = (slot / columns) - (rows / 2);
                auto position = SimVector(
                    centre.x + (intToSimScalar(pitch) * intToSimScalar(column)),
                    centre.y,
                    centre.z + (intToSimScalar(pitch) * intToSimScalar(rank)));
                position.y = simulation.terrain.getHeightAt(position.x, position.z);

                // The occupancy test inside trySpawnUnit never asks whether
                // the unit could move off the spot again. A crater wall is
                // empty ground as far as that is concerned, so slots on one
                // took units quite happily and then kept them: fourteen
                // Swatters a side sat out an entire run on the same rock,
                // too steep to leave, counted all the while towards the
                // hundred the harness thought it had in the fight. Ask the
                // movement class first -- the same question the game asks
                // before it puts a building down.
                auto footprintRect = simulation.computeFootprintRegion(position, definition.movementCollisionInfo);
                if (footprintRect.x < 0 || footprintRect.y < 0 || !simulation.canBeBuiltAt(movementClass, std::nullopt, false, static_cast<unsigned int>(footprintRect.x), static_cast<unsigned int>(footprintRect.y)))
                {
                    ++battleTestSpawnsBlocked;
                    continue;
                }

                // Completed, not a nanoframe: spawnUnit leaves a unit under
                // construction, and an unbuilt Peewee cannot walk, so they
                // simply piled up on the spawn.
                auto unit = spawnCompletedUnit(unitType, battleTestPlayers[i], position);
                if (!unit)
                {
                    ++battleTestSpawnsBlocked;
                    continue;
                }

                unit->get().fireOrders = UnitFireOrders::FireAtWill;
                unit->get().addOrder(MoveOrder(enemy));
                ++battleTestAlive[i];
                ++battleTestSpawned;
            }
        }

        // A heartbeat in the log every few seconds. A run of this thing
        // normally ends under taskkill /F, which lets nothing flush, so the
        // only evidence that survives is what was already on disk --
        // SimpleLogger writes and flushes a line at a time, so this does.
        //
        // On the wall clock rather than a frame count, because the frame rate
        // is the thing being pushed: counting frames went quiet for minutes
        // at a time at five hundred a side, which is exactly the run whose
        // log matters. The frames since the last line are worth having for
        // the same reason -- that ratio is the answer to "does it behave at
        // five hundred".
        if (sweepDue)
        {
            auto elapsed = now - battleTestLastLogTime;
            battleTestLastLogTime = now;
            std::string aliveText;
            for (std::size_t i = 0; i < battleTestAlive.size(); ++i)
            {
                if (i != 0)
                {
                    aliveText += " v ";
                }
                aliveText += std::to_string(battleTestAlive[i]);
            }
            LOG_INFO << "Battle test: wanted " << wanted << " a side, alive " << aliveText
                     << ", spawned " << battleTestSpawned
                     << ", blocked " << battleTestSpawnsBlocked
                     << ", culled " << battleTestCulled
                     << ", re-ordered " << battleTestReordered
                     << ", " << battleTestFramesSinceLog << " frames in " << elapsed << "ms";
            battleTestFramesSinceLog = 0;
        }
    }

    std::optional<UnitId> GameScene::spawnUnit(const std::string& unitType, PlayerId owner, const SimVector& position, std::optional<const std::reference_wrapper<SimAngle>> rotation)
    {
        return simulation.trySpawnUnit(unitType, owner, position, rotation);
    }

    unsigned int GameScene::debugSpawnHitPoints(const UnitDefinition& unitDefinition) const
    {
        auto percent = static_cast<unsigned int>(std::clamp(unitSpawnHealthPercent, 1, 100));
        auto points = (static_cast<std::uint64_t>(unitDefinition.maxHitPoints) * percent) / 100u;
        return static_cast<unsigned int>(std::max<std::uint64_t>(1u, points));
    }

    void GameScene::buildUnitTypeCategories()
    {
        if (!unitTypesByCategory.empty())
        {
            return;
        }

        // TEDClass is the game's own classification -- TANK, KBOT, VTOL, SHIP,
        // FORT, PLANT, ENERGY, METAL, CNSTR, WATER, SPECIAL, COMMANDER -- so
        // the grouping is the data's rather than one invented here. It is
        // written with stray whitespace in a few files, hence the trim.
        std::map<std::string, std::vector<std::pair<std::string, std::string>>> byCategory;
        for (const auto& [unitType, definition] : simulation.unitDefinitions)
        {
            auto category = toUpper(definition.tedClass);
            auto first = category.find_first_not_of(" \t");
            auto last = category.find_last_not_of(" \t");
            category = first == std::string::npos ? std::string() : category.substr(first, last - first + 1);
            if (category.empty())
            {
                category = "UNCLASSIFIED";
            }

            auto name = definition.unitName.empty() ? unitType : definition.unitName;
            byCategory[category].emplace_back(unitType, name);
        }

        for (auto& [category, units] : byCategory)
        {
            std::sort(units.begin(), units.end(), [](const auto& a, const auto& b) {
                return a.second < b.second;
            });
            unitTypesByCategory.emplace_back(category, std::move(units));
        }
    }

    void GameScene::renderUnitSpawnerWindow()
    {
        if (!showUnitSpawnerWindow)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(420.0f, 520.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Unit spawner", &showUnitSpawnerWindow))
        {
            ImGui::End();
            return;
        }

        buildUnitTypeCategories();

        // Owner, named with its side so it is obvious which team the unit
        // will fight for.
        std::string ownerLabel = "none";
        if (unitSpawnPlayer >= 0 && unitSpawnPlayer < getSize(simulation.players))
        {
            const auto& player = simulation.players[unitSpawnPlayer];
            ownerLabel = std::to_string(unitSpawnPlayer) + ": " + player.side
                + (player.type == GamePlayerType::Human ? " (human)" : " (computer)")
                + (PlayerId(unitSpawnPlayer) == localPlayerId ? " [you]" : "");
        }
        if (ImGui::BeginCombo("Owner", ownerLabel.c_str()))
        {
            for (Index i = 0; i < getSize(simulation.players); ++i)
            {
                const auto& player = simulation.players[i];
                auto label = std::to_string(i) + ": " + player.side
                    + (player.type == GamePlayerType::Human ? " (human)" : " (computer)")
                    + (PlayerId(i) == localPlayerId ? " [you]" : "");
                if (ImGui::Selectable(label.c_str(), unitSpawnPlayer == static_cast<int>(i)))
                {
                    unitSpawnPlayer = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::InputText("Filter", unitSpawnFilter, IM_ARRAYSIZE(unitSpawnFilter));
        auto filter = toUpper(std::string(unitSpawnFilter));

        ImGui::BeginChild("spawner unit list", ImVec2(0.0f, 300.0f), true);
        for (const auto& [category, units] : unitTypesByCategory)
        {
            // Which units survive the filter decides whether the category is
            // worth showing at all, so a search for "solar" does not leave
            // eleven empty headers to open.
            std::vector<const std::pair<std::string, std::string>*> matching;
            for (const auto& unit : units)
            {
                if (filter.empty()
                    || unit.first.find(filter) != std::string::npos
                    || toUpper(unit.second).find(filter) != std::string::npos)
                {
                    matching.push_back(&unit);
                }
            }
            if (matching.empty())
            {
                continue;
            }

            // Force every header open while filtering -- the point of typing
            // is to see what matched, not to then go opening headers -- but
            // only while filtering. This used to pass !filter.empty() with
            // ImGuiCond_Always unconditionally, which re-asserted "closed" on
            // every frame once the box was empty: a click opened the node and
            // the next frame shut it again.
            if (!filter.empty())
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }

            // The ID comes from the bare category and the count is only in the
            // format string, so a header keeps its open state when the filter
            // changes how many units it matches. Building the label into the
            // ID, as this did, made every distinct count a different node.
            if (ImGui::TreeNode(category.c_str(), "%s (%d)", category.c_str(), static_cast<int>(matching.size())))
            {
                for (const auto* unit : matching)
                {
                    // The name first, because that is what a person is looking
                    // for, and the code after it, because that is what the
                    // logs and the console use.
                    auto label = unit->second + "  [" + unit->first + "]";
                    if (ImGui::Selectable(label.c_str(), unit->first == unitSpawnType))
                    {
                        unitSpawnType = unit->first;
                    }
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();

        if (unitSpawnType.empty())
        {
            ImGui::TextUnformatted("Nothing selected");
        }
        else
        {
            const auto& definition = simulation.unitDefinitions.at(unitSpawnType);
            ImGui::Text("%s [%s]", definition.unitName.empty() ? unitSpawnType.c_str() : definition.unitName.c_str(), unitSpawnType.c_str());
            if (!definition.unitDescription.empty())
            {
                ImGui::TextWrapped("%s", definition.unitDescription.c_str());
            }
            ImGui::Text("%u hit points", definition.maxHitPoints);
        }

        ImGui::Separator();

        ImGui::SliderInt("Health %", &unitSpawnHealthPercent, 1, 100);
        if (!unitSpawnType.empty())
        {
            const auto& definition = simulation.unitDefinitions.at(unitSpawnType);
            ImGui::Text("spawns on %u of %u", debugSpawnHitPoints(definition), definition.maxHitPoints);
        }

        ImGui::Checkbox("Place on click (right-click to stop)", &unitSpawnOnClick);
        ImGui::Checkbox("Place finished (off: place a nanoframe)", &unitSpawnComplete);
        if (!unitSpawnComplete)
        {
            ImGui::TextUnformatted("(health applies to finished units only)");
        }

        if (ImGui::Button("Place one at the cursor"))
        {
            if (auto terrainPos = getMouseTerrainCoordinate())
            {
                placeDebugUnit(*terrainPos);
            }
        }

        ImGui::End();
    }

    void GameScene::placeDebugUnit(const SimVector& position)
    {
        if (unitSpawnType.empty() || !isValidUnitType(simulation, unitSpawnType))
        {
            return;
        }
        if (unitSpawnPlayer < 0 || unitSpawnPlayer >= getSize(simulation.players))
        {
            return;
        }

        // The list offers every unit type the data defines, and some of those
        // name a model or a script that did not load. Spawning one of those
        // would take the game down, so refuse it here: this is a tool for
        // poking at units, not a way to crash out of a game.
        const auto& unitDefinition = simulation.unitDefinitions.at(unitSpawnType);
        if (simulation.unitModelDefinitions.find(unitDefinition.objectName) == simulation.unitModelDefinitions.end())
        {
            LOG_WARN << "Cannot place " << unitSpawnType << ": its model " << unitDefinition.objectName << " is not loaded";
            return;
        }
        if (simulation.unitScriptDefinitions.find(unitSpawnType) == simulation.unitScriptDefinitions.end())
        {
            LOG_WARN << "Cannot place " << unitSpawnType << ": its script is not loaded";
            return;
        }

        auto owner = PlayerId(unitSpawnPlayer);
        if (unitSpawnComplete)
        {
            if (auto unit = spawnCompletedUnit(unitSpawnType, owner, position))
            {
                // A nanoframe's hit points track its build progress, so this
                // only means anything for a finished unit; the window says so.
                unit->get().hitPoints = debugSpawnHitPoints(unitDefinition);
            }
        }
        else
        {
            // Left as a nanoframe, so a builder can be told to finish it.
            spawnUnit(unitSpawnType, owner, position, std::nullopt);
        }
    }

    void GameScene::renderUnitPlacer()
    {
        if (!ImGui::CollapsingHeader("Place units"))
        {
            return;
        }
        ImGui::Indent();

        if (allUnitTypes.empty())
        {
            for (const auto& [unitType, _] : simulation.unitDefinitions)
            {
                allUnitTypes.push_back(unitType);
            }
            std::sort(allUnitTypes.begin(), allUnitTypes.end());
        }

        // Owner: every player in the game, named with its side so it is
        // obvious which team a unit will fight for.
        std::string ownerLabel = "none";
        if (unitSpawnPlayer >= 0 && unitSpawnPlayer < getSize(simulation.players))
        {
            const auto& player = simulation.players[unitSpawnPlayer];
            ownerLabel = std::to_string(unitSpawnPlayer) + ": " + player.side
                + (player.type == GamePlayerType::Human ? " (human)" : " (computer)")
                + (PlayerId(unitSpawnPlayer) == localPlayerId ? " [you]" : "");
        }
        if (ImGui::BeginCombo("Owner", ownerLabel.c_str()))
        {
            for (Index i = 0; i < getSize(simulation.players); ++i)
            {
                const auto& player = simulation.players[i];
                auto label = std::to_string(i) + ": " + player.side
                    + (player.type == GamePlayerType::Human ? " (human)" : " (computer)")
                    + (PlayerId(i) == localPlayerId ? " [you]" : "");
                if (ImGui::Selectable(label.c_str(), unitSpawnPlayer == static_cast<int>(i)))
                {
                    unitSpawnPlayer = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::InputText("Filter", unitSpawnFilter, IM_ARRAYSIZE(unitSpawnFilter));
        auto filter = toUpper(std::string(unitSpawnFilter));

        ImGui::BeginChild("unit type list", ImVec2(0.0f, 160.0f), true);
        for (const auto& unitType : allUnitTypes)
        {
            if (!filter.empty() && unitType.find(filter) == std::string::npos)
            {
                continue;
            }
            if (ImGui::Selectable(unitType.c_str(), unitType == unitSpawnType))
            {
                unitSpawnType = unitType;
            }
        }
        ImGui::EndChild();

        ImGui::LabelText("Selected", "%s", unitSpawnType.empty() ? "none" : unitSpawnType.c_str());
        ImGui::Checkbox("Place on click (right-click to stop)", &unitSpawnOnClick);
        ImGui::Checkbox("Place finished (off: place a nanoframe)", &unitSpawnComplete);

        if (ImGui::Button("Place one at the cursor"))
        {
            if (auto terrainPos = getMouseTerrainCoordinate())
            {
                placeDebugUnit(*terrainPos);
            }
        }

        // The old typed entry, kept for when the name is already known.
        if (ImGui::InputText("Type a name and press enter", unitSpawnText, IM_ARRAYSIZE(unitSpawnText), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            auto text = toUpper(std::string(unitSpawnText));
            if (!text.empty() && isValidUnitType(simulation, text))
            {
                unitSpawnType = text;
                if (auto terrainPos = getMouseTerrainCoordinate())
                {
                    placeDebugUnit(*terrainPos);
                }
            }
            ImGui::SetKeyboardFocusHere(-1);
        }

        if (ImGui::Button("Kill every unit of the chosen owner"))
        {
            if (unitSpawnPlayer >= 0 && unitSpawnPlayer < getSize(simulation.players))
            {
                std::vector<UnitId> doomed;
                for (const auto& [unitId, unit] : simulation.units)
                {
                    if (unit.isOwnedBy(PlayerId(unitSpawnPlayer)) && unit.isAlive())
                    {
                        doomed.push_back(unitId);
                    }
                }
                for (auto unitId : doomed)
                {
                    simulation.killUnit(unitId);
                }
            }
        }

        ImGui::Unindent();
    }

    std::optional<std::reference_wrapper<UnitState>> GameScene::spawnCompletedUnit(const std::string& unitType, PlayerId owner, const SimVector& position)
    {
        auto unitId = spawnUnit(unitType, owner, position, std::nullopt);
        if (unitId)
        {
            auto& unit = getUnit(*unitId);
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            // units start as unbuilt nanoframes,
            // we we need to convert it immediately into a completed unit.
            unit.finishBuilding(unitDefinition);

            return unit;
        }

        return std::nullopt;
    }
}
