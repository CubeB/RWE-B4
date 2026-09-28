// The tick-indexed scenario runner: a headless harness that drives the real
// GameScene -- the same input handlers, selection, cursor modes, panels and
// simulation as rwe.exe -- at chosen ticks, then asserts on what came back.
//
// It exists for the faults that cannot be pulled out into a pure function and
// unit-tested: a fault that lives in a sequence of real handler calls, or in
// a panel's lifecycle across a rebuild. A sim question belongs in
// sim_test_util and Catch2; this is for the interface ones.
//
//   scenario --list
//   scenario --run 342-active-tab-stays-pressed --data-path <dir>
//   scenario --all --data-path <dir>
//
// Scenarios are C++ functions registered below. Each runs under Xvfb (the
// real renderer needs a GL context to load its assets, even though headless
// mode skips drawing), exits non-zero on a failed assertion, and on the same
// seed writes the same RWE_HASH_LOG as any other run.
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <rwe/GameLaunch.h>
#include <rwe/GlobalConfig.h>
#include <rwe/PathMapping.h>
#include <rwe/game/ScenarioDriver.h>
#include <rwe/sim/SimVector.h>
#include <rwe/util.h>
#include <rwe/util/CrashHandler.h>
#include <rwe/util/OpaqueArgs.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>

namespace fs = std::filesystem;

namespace
{
    rwe::PathMapping constructDefaultPathMapping()
    {
        rwe::PathMapping m;
        m.ai = "ai";
        m.anims = "anims";
        m.bitmaps = "bitmaps";
        m.camps = "camps";
        m.downloads = "download";
        m.features = "features";
        m.fonts = "fonts";
        m.gamedata = "gamedata";
        m.guis = "guis";
        m.maps = "maps";
        m.objects3d = "objects3d";
        m.palettes = "palettes";
        m.scripts = "scripts";
        m.sounds = "sounds";
        m.textures = "textures";
        m.unitpics = "unitpics";
        m.units = "units";
        m.weapons = "weapons";
        return m;
    }

    std::shared_ptr<rwe::SimpleLogger> createLogger(const fs::path& logDir)
    {
        return std::make_shared<rwe::SimpleLogger>((logDir / "scenario.log").string(), true);
    }

    std::vector<fs::path> resolveDataPaths(const rwe::OpaqueArgs& args, const fs::path& localDataPath)
    {
        std::vector<fs::path> paths;
        auto given = args.getMulti("data-path");
        if (!given.empty())
        {
            paths.insert(paths.end(), given.begin(), given.end());
        }
        else
        {
            paths.emplace_back(localDataPath) /= "Data";
        }
        return paths;
    }

    /**
     * Whether a scenario runs in the own-clock game mode (#429). The mode is
     * chosen before the scene exists and a step runs only once it does, so a
     * scenario names itself here rather than switching mode from inside a
     * step. Every scenario not named is an ordinary lockstep game.
     */
    bool scenarioRunsOwnClock(const std::string& name)
    {
        return name == "own-clock-ticks-and-orders";
    }

    void registerScenarios()
    {
        // The active BUILD/ORDERS tab has to stay pressed across the panel
        // rebuild a tab switch causes. Clicking ORDERS sets guiInfo.section
        // and asks for a new panel; the old panel, and the toggledOn it held,
        // is destroyed. Issue #342.
        rwe::registerScenario("342-active-tab-stays-pressed", [](rwe::Scenario& s) {
            s.at(1, [](rwe::ScenarioDriver& d) {
                auto unit = d.commander(0);
                d.require(unit.has_value(), "the local player has a commander");
                if (unit)
                {
                    d.select(*unit);
                }
            });
            s.at(30, [](rwe::ScenarioDriver& d) { d.clickGadget("ORDERS"); });
            s.after(31, [](rwe::ScenarioDriver& d) {
                d.require(d.gadget("BUILD").found, "BUILD tab is on the panel");
                d.require(d.gadget("ORDERS").found, "ORDERS tab is on the panel");
                d.require(!d.gadget("BUILD").toggledOn, "BUILD is not the pressed tab");
                d.require(d.gadget("ORDERS").toggledOn, "ORDERS is the pressed tab");
            });
        });

        // Placing a unit while the game is paused, then selecting it, used to
        // take the game down with "Gui info not found for unit": a unit's
        // panel state comes from the unit-spawned event, which is only drained
        // inside a simulation tick, and a paused game runs no ticks. Selecting
        // it before the game resumes has to create the state on demand. This
        // pins the fix from the ROADMAP's round 10.
        rwe::registerScenario("paused-place-select", [](rwe::Scenario& s) {
            s.at(5, [](rwe::ScenarioDriver& d) {
                d.key(SDLK_PAUSE);
                d.require(d.isPaused(), "the game is paused");
                auto placed = d.spawnNearCamera("ARMPW");
                d.require(placed.has_value(), "a unit was placed");
                if (placed)
                {
                    d.select(*placed);
                    d.require(d.selectedCount() == 1, "the placed unit is selected while paused");
                }
            });
            s.after(5, [](rwe::ScenarioDriver& d) {
                d.require(d.selectedCount() == 1, "the placed unit is still selected");
            });
        });

        // The own-clock game mode (#429): a live game whose scene advances on
        // its own clock like a skirmish, with no lockstep command gate, no
        // hash exchange, no desync report and no save, for playing against a
        // peer that runs its own simulation and waits for nobody. With no
        // peer it has to behave exactly like a skirmish: the clock advances
        // at the chosen speed and the local player's own orders run.
        rwe::registerScenario("own-clock-ticks-and-orders", [](rwe::Scenario& s) {
            struct State
            {
                std::optional<rwe::UnitId> commander;
                rwe::SimVector start;
            };
            auto state = std::make_shared<State>();

            s.at(1, [state](rwe::ScenarioDriver& d) {
                state->commander = d.commander(0);
                d.require(state->commander.has_value(), "the local player has a commander");
                if (!state->commander)
                {
                    return;
                }
                auto position = d.unitPosition(*state->commander);
                d.require(position.has_value(), "the commander has a position");
                if (!position)
                {
                    return;
                }
                state->start = *position;
                d.moveOrder(
                    *state->commander,
                    rwe::SimVector(state->start.x + rwe::SimScalar(300.0f), state->start.y, state->start.z));
            });

            // The headless loop hands the scene exactly one tick's worth a
            // frame, so at the default speed this fires on about the
            // thirty-second frame. Then the peer asks for twice the speed and
            // the same ticks have to arrive in about half the frames.
            s.at(31, [](rwe::ScenarioDriver& d) {
                d.require(d.frameCount() <= 35, "at the default speed the clock advances a tick a frame");
                d.peerGameSpeed(20);
            });

            s.at(91, [state](rwe::ScenarioDriver& d) {
                d.require(d.frameCount() <= 75, "at twice the speed the clock advances about two ticks a frame");
                if (!state->commander)
                {
                    return;
                }
                auto position = d.unitPosition(*state->commander);
                d.require(position.has_value(), "the commander is still on the map");
                if (position)
                {
                    auto moved = (position->x != state->start.x) || (position->z != state->start.z);
                    d.require(moved, "the local player's move order ran");
                }
            });
            s.endAt(91);
        });

        // The side panel's idle-builder sign (#418): a count of the local
        // player's construction units that have nothing to do, and a click that
        // goes to the next one. Two things a pure-function test cannot reach.
        // The count has to arrive on a widget that did not exist when the game
        // started, because the panel is rebuilt every time the selection
        // changes -- so the sign is put on each new panel and has to be found
        // on the current one afterwards. And the click has to travel the real
        // route, a mouseDown and a mouseUp through the scene's own handlers
        // into the panel's group messages, and come out the far side as a
        // selection and a camera move.
        //
        // One construction bot is spawned because the commander alone gives the
        // count a single entry, and a rotation over one entry cannot be told
        // apart from no rotation at all.
        rwe::registerScenario("418-idle-builder-sign", [](rwe::Scenario& s) {
            // How far apart two points are in the world plane, squared. The
            // camera is clamped to the terrain and cannot always be put exactly
            // on a unit, so "the view is nearer the builder the sign named than
            // to the other one" is the true statement about what a click does,
            // and squaring keeps it a comparison rather than a square root.
            auto squaredFlatDistance = [](const rwe::SimVector& a, const rwe::SimVector& b) {
                auto dx = a.x - b.x;
                auto dz = a.z - b.z;
                return dx * dx + dz * dz;
            };

            struct State
            {
                std::optional<rwe::UnitId> bot;
                std::optional<rwe::UnitId> firstClick;
            };
            auto state = std::make_shared<State>();

            s.at(1, [state](rwe::ScenarioDriver& d) {
                // The commander on its own is a construction unit with an empty
                // order queue, so the sign is showing something before anything
                // is spawned. If it is not, the sign is not being updated at
                // all and nothing after this can be believed.
                auto before = d.idleBuilderCount();
                d.require(before.has_value(), "the side panel has room for the idle-builder sign");
                d.require(
                    before.value_or(-1) == 1,
                    "the commander is the only idle construction unit to begin with, not " + std::to_string(before.value_or(-1)));

                // ARMCK is the level-one construction bot, the name the AI's own
                // side-units table carries for it. One only: the spawner drops
                // every unit on the same spot beside the camera, so a second
                // would find the first standing there and refuse.
                state->bot = d.spawnNearCamera("ARMCK");
                d.require(state->bot.has_value(), "a construction bot was placed");
            });

            s.at(2, [](rwe::ScenarioDriver& d) {
                d.require(
                    d.idleBuilderCount() == 2,
                    "the sign counts the commander and the construction bot, not " + std::to_string(d.idleBuilderCount().value_or(-1)));
            });

            s.at(3, [state, squaredFlatDistance](rwe::ScenarioDriver& d) {
                auto before = d.cameraPosition();
                d.clickGadget("IDLEBUILDERS");

                state->firstClick = d.selectedUnit();
                d.require(d.selectedCount() == 1, "the sign's click selected exactly one unit");
                d.require(state->firstClick.has_value(), "the sign's click selected a unit the driver can name");

                // And the view went with it. The camera is clamped to keep the
                // world viewport on the terrain, so a builder standing at the
                // edge of the map cannot be centred on exactly and "it arrived"
                // is not a statement this can make. What it can make is the
                // half that survives the clamp: the click left the view no
                // further from the builder the sign named than it found it,
                // which is what setting the camera on that builder means once
                // the constraint has had its say.
                auto after = d.cameraPosition();
                auto onNamed = state->firstClick ? d.unitPosition(*state->firstClick) : std::nullopt;
                d.require(after.has_value() && onNamed.has_value() && before.has_value(), "the camera and the builder it jumped to both have positions");
                if (after && onNamed && before)
                {
                    d.require(
                        squaredFlatDistance(*after, *onNamed) <= squaredFlatDistance(*before, *onNamed),
                        "the click left the view no further from the builder it named than it found it");
                }
            });

            s.at(4, [](rwe::ScenarioDriver& d) {
                // Back to the main panel, and the sign is on this one. It is
                // deliberately not on a build page: those are grids of buttons
                // out of a different gui file -- ARMCOM1's runs from row 27 to
                // 237 -- and RWE has no claim on any part of one, so the sign
                // is left off rather than laid over shipped art. Selecting a
                // builder therefore takes the sign off the screen, and this is
                // the way back to it.
                d.key(SDLK_ESCAPE);
                d.require(d.selectedCount() == 0, "escape cleared the selection the sign made");
            });

            s.at(5, [state, squaredFlatDistance](rwe::ScenarioDriver& d) {
                // The second click moves on rather than coming back to the same
                // builder, which is the whole difference between a rotation and
                // a shortcut.
                d.require(d.idleBuilderCount() == 2, "the sign came back on the main panel with the count it had");
                auto before = d.cameraPosition();
                d.clickGadget("IDLEBUILDERS");
                auto secondClick = d.selectedUnit();
                d.require(d.selectedCount() == 1, "the second click selected exactly one unit");
                d.require(secondClick.has_value(), "the second click selected a unit the driver can name");
                d.require(
                    !state->firstClick.has_value() || secondClick != state->firstClick,
                    "the second click moved on to a different builder");

                auto after = d.cameraPosition();
                auto onSecond = secondClick ? d.unitPosition(*secondClick) : std::nullopt;
                d.require(after.has_value() && onSecond.has_value() && before.has_value(), "the camera and the second builder both have positions");
                if (after && onSecond && before)
                {
                    d.require(
                        squaredFlatDistance(*after, *onSecond) <= squaredFlatDistance(*before, *onSecond),
                        "the second click brought the view to the other builder");
                }
            });

            s.at(6, [](rwe::ScenarioDriver& d) {
                // The count is live: give the builder the second click landed on
                // something to do, and the number on the sign has to fall by
                // one, with nothing else having changed.
                auto busy = d.selectedUnit();
                d.require(busy.has_value(), "there is a selected builder to give work to");
                if (!busy)
                {
                    return;
                }
                auto at = d.unitPosition(*busy);
                d.require(at.has_value(), "the busy builder has a position");
                if (at)
                {
                    d.moveOrder(*busy, rwe::SimVector(at->x + rwe::SimScalar(240.0f), at->y, at->z));
                }
            });

            s.at(7, [](rwe::ScenarioDriver& d) {
                // Back to the main panel again, so the count is read off a
                // widget that is actually on screen.
                d.key(SDLK_ESCAPE);
            });

            s.after(8, [](rwe::ScenarioDriver& d) {
                // Read several ticks after the order went out, because that
                // order is a command: it rides out with the tick's set and
                // lands on the unit a tick or two later, and the sign only
                // ever shows what the simulation has already decided.
                d.require(
                    d.idleBuilderCount() == 1,
                    "a builder given an order has left the count, and the count reads " + std::to_string(d.idleBuilderCount().value_or(-1)));
            });

            s.endAt(8);
        });
    }

    /** Runs one scenario and says whether it passed. */
    bool runOne(
        const std::string& name,
        const std::vector<fs::path>& dataPaths,
        const fs::path& localDataPath,
        const std::string& mapName,
        const std::optional<unsigned int>& seed)
    {
        rwe::GameParameters parameters(mapName, 0);
        parameters.localNetworkPort = "0";

        if (scenarioRunsOwnClock(name))
        {
            parameters.netMode = rwe::NetMode::OwnClock;
        }

        // One local human, so the input path is exactly a player's: the click
        // goes into localPlayerCommandBuffer and out as an ordinary command in
        // the lockstep stream. No AI, so nothing else moves the world.
        parameters.players[0] = rwe::parsePlayerInfoFromArg("scenario;Human;ARM;0");
        parameters.scenarioName = name;
        parameters.randomSeed = seed;

        rwe::GlobalConfig config;
        config.musicEnabled = false;
        config.soundMode = 0;
        config.shadows = false;
        config.antiAlias = false;

        // The outcome is process-wide and is only reset by a driver being
        // built. A run that never builds a GameScene -- a load failure, a quit
        // event -- would otherwise leave this reading the previous scenario's
        // pass, so it is cleared here, before anything can look at it.
        rwe::resetScenarioOutcome();

        std::cout << "SCENARIO-START " << name << "\n";
        int code = 0;
        try
        {
            code = rwe::run(
                dataPaths,
                constructDefaultPathMapping(),
                parameters,
                1280,
                800,
                rwe::WindowMode::Bordered,
                (localDataPath / "imgui.ini").string(),
                config);
        }
        catch (const std::exception& e)
        {
            std::cerr << "SCENARIO-FAIL " << name << ": " << e.what() << "\n";
            return false;
        }

        if (code != 0)
        {
            std::cerr << "SCENARIO-FAIL " << name << ": rwe::run returned " << code << "\n";
            return false;
        }

        const auto& outcome = rwe::scenarioOutcome();
        if (!outcome.started)
        {
            std::cerr << "SCENARIO-FAIL " << name << ": did not start\n";
            return false;
        }
        // Each failure was already printed as it was recorded; saying so once
        // more here would double every line of the report.
        if (!outcome.failures.empty() || !outcome.reachedEnd)
        {
            return false;
        }

        std::cout << "SCENARIO-PASS " << name << "\n";
        return true;
    }
}

int main(int argc, char* argv[])
{
    try
    {
        auto localDataPath = rwe::getLocalDataPath();
        if (!localDataPath)
        {
            throw std::runtime_error("Failed to determine local data path");
        }
        fs::create_directories(*localDataPath);

        rwe::OpaqueArgs args;
        args.parse(argc, argv);
        args.parseConfig((*localDataPath / "rwe.cfg").string());

        registerScenarios();

        if (args.isHelpRequested())
        {
            std::cout << "scenario --list\n"
                      << "scenario --run <name> --data-path <dir> [--seed n] [--map <name>]\n"
                      << "scenario --all --data-path <dir> [--seed n] [--map <name>]\n\n"
                      << "Drives the real GameScene headlessly at chosen ticks and asserts on it.\n"
                      << "Needs Total Annihilation game data, and a GL context: run it under Xvfb\n"
                      << "on a machine without a display.\n";
            return 0;
        }

        if (args.getBool("list"))
        {
            for (const auto& name : rwe::scenarioNames())
            {
                std::cout << name << "\n";
            }
            return 0;
        }

        auto names = rwe::scenarioNames();
        auto runName = args.getString("run", "");
        bool runAll = args.getBool("all");
        if (!runAll && runName.empty())
        {
            std::cerr << "Nothing to do: pass --run <name> or --all (--list prints the names).\n";
            return 2;
        }
        if (runAll)
        {
            names = rwe::scenarioNames();
        }
        else
        {
            if (std::find(names.begin(), names.end(), runName) == names.end())
            {
                std::cerr << "No scenario called '" << runName << "'. --list prints them.\n";
                return 2;
            }
            names = {runName};
        }

        auto logger = createLogger(*localDataPath);
        rwe::setGlobalLogger(logger);
        rwe::installCrashHandler(*localDataPath);

        auto dataPaths = resolveDataPaths(args, *localDataPath);
        auto mapName = args.getString("map", "Coast To Coast");

        std::optional<unsigned int> seed;
        if (auto seedString = args.getString("seed", ""); !seedString.empty())
        {
            seed = static_cast<unsigned int>(std::stoul(seedString));
        }

        bool allPassed = true;
        for (const auto& name : names)
        {
            allPassed = runOne(name, dataPaths, *localDataPath, mapName, seed) && allPassed;
        }

        return allPassed ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << "\n";
        LOG_ERROR << "scenario failed: " << e.what();
        return 1;
    }
}
