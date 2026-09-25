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
#include <rwe/GameLaunch.h>
#include <rwe/GlobalConfig.h>
#include <rwe/PathMapping.h>
#include <rwe/game/ScenarioDriver.h>
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
