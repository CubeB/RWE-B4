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
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <rwe/GameLaunch.h>
#include <rwe/GlobalConfig.h>
#include <rwe/PathMapping.h>
#include <rwe/game/ScenarioDriver.h>
#include <rwe/io/pcx/pcx.h>
#include <rwe/scene/PosterCapture.h>
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

    /**
     * Whether a scenario runs with an opposing seat, for fixtures that need a
     * foreign unit to own (#899). The seat is a computer player so the
     * ordinary one-human rule still holds and no second local input exists.
     */
    bool scenarioNeedsOpposingSeat(const std::string& name)
    {
        return name == "899-hidden-extractor-bubbles" || name == "899-owned-extractor-bubbles";
    }

    void registerScenarios()
    {
        rwe::registerScenario("898-water-build-height", [](rwe::Scenario& s) {
            auto expected = std::make_shared<std::optional<rwe::SimVector>>();
            s.at(1, [expected](rwe::ScenarioDriver& d) {
                *expected = d.placeWaterBuildAndCheckFeedback("ARMTIDE");
            });
            s.at(20, [expected](rwe::ScenarioDriver& d) {
                auto builder = d.commander(0);
                d.require(builder.has_value() && expected->has_value(), "the placement fixture completed");
                if (!builder || !*expected)
                {
                    return;
                }
                auto queued = d.queuedBuildPosition(*builder, "ARMTIDE");
                d.require(queued.has_value(), "the real click reached the command stream as a queued build");
                if (queued)
                {
                    d.require(*queued == **expected, "the queued order retains the preview's position and height");
                }
            });
            s.endAt(20);
        });

        rwe::registerScenario("653-bookmark-exits-tracking", [](rwe::Scenario& s) {
            auto saved = std::make_shared<std::optional<rwe::SimVector>>();
            s.at(1, [saved](rwe::ScenarioDriver& d) {
                auto commander = d.commander(0);
                d.require(commander.has_value(), "a shipped local commander is available");
                if (!commander)
                {
                    return;
                }
                d.select(*commander);
                d.key(SDLK_T);
                d.require(d.isCameraTracking(), "T starts unit following");
                *saved = d.cameraPosition();
                d.keyDown(SDLK_LCTRL);
                d.key(SDLK_F5);
                d.keyUp(SDLK_LCTRL);
                d.require(d.isCameraTracking(), "saving a bookmark retains following");
            });
            s.at(2, [](rwe::ScenarioDriver& d) {
                d.key(SDLK_F5);
                d.require(!d.isCameraTracking(), "stored bookmark recall stops following");
            });
            s.after(3, [saved](rwe::ScenarioDriver& d) {
                d.require(!d.isCameraTracking(), "following remains stopped after camera updates");
                d.require(saved->has_value() && d.cameraPosition() == *saved, "the camera retains the stored position");
            });
            s.at(4, [](rwe::ScenarioDriver& d) {
                d.key(SDLK_T);
                d.require(d.isCameraTracking(), "unit following can be entered again");
                d.key(SDLK_N);
                d.require(d.isCameraTracking(), "n retains following, as the reference establishes");
                d.key(SDLK_F3);
                d.require(d.isCameraTracking(), "F3 retains following");
            });
            s.after(6, [](rwe::ScenarioDriver& d) {
                d.require(d.isCameraTracking(), "n/F3 do not cancel later following updates");
            });
            s.endAt(6);
        });

        // The TA 3.9.01 patch's selection keys (#884). At the start the
        // commander is the only idle builder, and it carries no CTRL_B token,
        // so the retail Ctrl+B would select nothing at all here.
        rwe::registerScenario("884-ta-3901-selection-keys", [](rwe::Scenario& s) {
            auto factory = std::make_shared<std::optional<rwe::UnitId>>();
            auto tank = std::make_shared<std::optional<rwe::UnitId>>();
            s.at(1, [factory, tank](rwe::ScenarioDriver& d) {
                *factory = d.spawnNearCamera("ARMVP");
                *tank = d.spawnNearCamera("ARMSTUMP", -96.0f, 96.0f);
                d.require(factory->has_value(), "a factory was spawned");
                d.require(tank->has_value(), "a tank was spawned");
            });
            s.at(2, [factory](rwe::ScenarioDriver& d) {
                d.keyDown(SDLK_LCTRL);
                d.key(SDLK_B);
                d.require(d.selectedUnit() == d.commander(0), "Ctrl+B selects the idle commander");
                d.key(SDLK_F);
                d.require(d.selectedUnit() == *factory, "Ctrl+F selects the idle factory");
                d.key(SDLK_F);
                d.require(d.selectedUnit() == *factory, "Ctrl+F wraps round to the only idle factory");
                d.key(SDLK_S);
                d.keyUp(SDLK_LCTRL);
                d.require(d.selectedCount() == 2, "Ctrl+S takes the commander and the tank but not the unarmed factory");
            });
            s.endAt(3);
        });

        // The original's Ctrl+letter family (#891): any letter is a
        // `CTRL_<letter>` category lookup and Shift adds instead of
        // replacing. Ctrl+B/F stay the 3.9.01 patch's idle walks when
        // unshifted (#884); their shifted form is the category command.
        // The digits follow the saved `SwitchAlt` preference (#907,
        // TOTALA-EXE-KEYBOARD.md section 75): the shipped default is build
        // pages on the bare digits and squads on Alt, and `+SwitchAlt` swaps
        // them.
        rwe::registerScenario("891-keybindings", [](rwe::Scenario& s) {
            struct Spawned
            {
                std::optional<rwe::UnitId> tank;
                std::optional<rwe::UnitId> factory;
                std::optional<rwe::UnitId> builder;
            };
            auto units = std::make_shared<Spawned>();

            s.at(1, [units](rwe::ScenarioDriver& d) {
                units->tank = d.spawnNearCamera("ARMSTUMP", -160.0f, 0.0f);
                units->factory = d.spawnNearCamera("ARMVP", 160.0f, 0.0f);
                units->builder = d.spawnNearCamera("ARMCK", 0.0f, 160.0f);
                d.require(units->tank.has_value(), "a CTRL_W tank was spawned");
                d.require(units->factory.has_value(), "a CTRL_F factory was spawned");
                d.require(units->builder.has_value(), "a CTRL_B builder was spawned");
            });

            s.at(2, [units](rwe::ScenarioDriver& d) {
                auto commander = d.commander(0);
                d.require(commander.has_value(), "the local player has a commander");
                if (!commander || !units->tank || !units->factory || !units->builder)
                {
                    return;
                }

                d.select(*commander);
                d.keyDown(SDLK_LCTRL);
                d.keyDown(SDLK_LSHIFT);
                d.key(SDLK_W);
                d.keyUp(SDLK_LSHIFT);
                d.keyUp(SDLK_LCTRL);
                d.require(d.selectedCount() == 2, "Ctrl+Shift+W adds the armed mobile to the commander");

                d.keyDown(SDLK_LCTRL);
                d.keyDown(SDLK_LSHIFT);
                d.key(SDLK_E);
                d.keyUp(SDLK_LSHIFT);
                d.keyUp(SDLK_LCTRL);
                d.require(d.selectedCount() == 2, "Ctrl+Shift+E keeps the selection for an unknown category");

                d.keyDown(SDLK_LCTRL);
                d.key(SDLK_E);
                d.keyUp(SDLK_LCTRL);
                d.require(d.selectedCount() == 0, "Ctrl+E replaces: an unknown category clears the eligible pool");

                d.keyDown(SDLK_LCTRL);
                d.keyDown(SDLK_LSHIFT);
                d.key(SDLK_B);
                d.keyUp(SDLK_LSHIFT);
                d.keyUp(SDLK_LCTRL);
                d.require(d.selectedUnit() == units->builder, "Ctrl+Shift+B selects every CTRL_B construction unit");

                d.key(SDLK_ESCAPE);
                d.keyDown(SDLK_LCTRL);
                d.keyDown(SDLK_LSHIFT);
                d.key(SDLK_F);
                d.keyUp(SDLK_LSHIFT);
                d.keyUp(SDLK_LCTRL);
                d.require(d.selectedUnit() == units->factory, "Ctrl+Shift+F selects every CTRL_F factory");
            });

            s.at(3, [units](rwe::ScenarioDriver& d) {
                auto commander = d.commander(0);
                d.require(commander.has_value() && units->tank.has_value() && units->factory.has_value(),
                    "the fixture survived to the page test");
                if (!commander || !units->tank || !units->factory)
                {
                    return;
                }

                // The shipped default: bit 8 clear, so a bare digit opens the
                // selected builder's build-menu page.
                d.key(SDLK_ESCAPE);
                d.select(*commander);
                d.require(d.buildPage(*commander) == 0u, "the commander starts on build page one");

                d.key(SDLK_2);
                d.require(d.buildPage(*commander) == 1u, "a bare digit opens its build page");
                d.require(d.showsBuildPage(*commander), "and switches the panel to BUILD");
                d.key(SDLK_1);
                d.require(d.buildPage(*commander) == 0u, "digit 1 returns to page one");

                d.key(SDLK_9);
                d.require(d.buildPage(*commander) == 0u, "a page the builder does not have is left alone");

                // Alt is the squad half under the default: Ctrl+1 binds the
                // commander to squad 1.
                d.keyDown(SDLK_LCTRL);
                d.key(SDLK_1);
                d.keyUp(SDLK_LCTRL);
                d.require(d.selectedUnit() == commander, "Ctrl+1 assigns the squad and keeps the selection");

                d.select(*units->tank);
                d.keyDown(SDLK_LALT);
                d.keyDown(SDLK_LSHIFT);
                d.key(SDLK_1);
                d.keyUp(SDLK_LSHIFT);
                d.keyUp(SDLK_LALT);
                d.require(d.selectedCount() == 2, "Alt+Shift+1 adds the squad to the selection");

                d.key(SDLK_ESCAPE);
                d.select(*units->tank);
                d.keyDown(SDLK_LALT);
                d.key(SDLK_1);
                d.keyUp(SDLK_LALT);
                d.require(d.selectedUnit() == commander, "Alt+1 recalls the squad instead of opening a page");

                // Bare digits do not recall under the default: 1 is the
                // build-page half, and the tank has no build pages.
                d.key(SDLK_ESCAPE);
                d.select(*units->tank);
                d.key(SDLK_1);
                d.require(d.selectedUnit() == units->tank, "a bare digit does not recall the squad");

                // There is no squad 0 in the original, and none here: neither
                // the assign nor either recall form binds or recalls one.
                d.keyDown(SDLK_LCTRL);
                d.key(SDLK_0);
                d.keyUp(SDLK_LCTRL);
                d.require(d.selectedUnit() == units->tank, "Ctrl+0 assigns nothing");
                d.keyDown(SDLK_LALT);
                d.key(SDLK_0);
                d.keyUp(SDLK_LALT);
                d.require(d.selectedUnit() == units->tank, "Alt+0 recalls no squad 0");
                d.key(SDLK_0);
                d.require(d.selectedUnit() == units->tank, "a bare 0 does nothing");
            });

            s.at(4, [units](rwe::ScenarioDriver& d) {
                auto commander = d.commander(0);
                d.require(commander.has_value() && units->tank.has_value() && units->factory.has_value(),
                    "the fixture survived to the SwitchAlt test");
                if (!commander || !units->tank || !units->factory)
                {
                    return;
                }

                // A bare +SwitchAlt flips the preference and saves it, and
                // says what it now is.
                d.require(!d.switchAltEnabled(), "the default arrangement is bit-clear");
                const auto linesBefore = d.consoleMessageCount();
                d.key(SDLK_RETURN);
                d.textInput("+switchalt");
                d.key(SDLK_RETURN);
                d.require(d.switchAltEnabled(), "+SwitchAlt flips the bit on");
                d.require(d.consoleMessageCount() == linesBefore + 1, "and prints the new state");

                // Swapped: the bare digit recalls the squad.
                d.key(SDLK_ESCAPE);
                d.select(*units->tank);
                d.key(SDLK_1);
                d.require(d.selectedUnit() == commander, "a bare digit recovers the squad");

                // And Alt is now the build-page half.
                d.key(SDLK_ESCAPE);
                d.select(*commander);
                d.keyDown(SDLK_LALT);
                d.key(SDLK_1);
                d.keyUp(SDLK_LALT);
                d.require(d.buildPage(*commander) == 0u, "Alt+1 opens page one");
                d.require(d.showsBuildPage(*commander), "Alt+1 opens the build menu");
                d.keyDown(SDLK_LALT);
                d.key(SDLK_2);
                d.keyUp(SDLK_LALT);
                d.require(d.buildPage(*commander) == 1u, "Alt+2 opens page two");

                // Ctrl still assigns, and Shift on a bare digit still adds the
                // squad to the selection.
                d.key(SDLK_ESCAPE);
                d.select(*units->tank);
                d.keyDown(SDLK_LSHIFT);
                d.key(SDLK_1);
                d.keyUp(SDLK_LSHIFT);
                d.require(d.selectedCount() == 2, "Shift+digit adds the squad");
                d.keyDown(SDLK_LCTRL);
                d.key(SDLK_3);
                d.keyUp(SDLK_LCTRL);
                d.key(SDLK_ESCAPE);
                d.select(*units->factory);
                d.key(SDLK_3);
                d.require(d.selectedCount() == 2, "Ctrl+digit still assigns and a bare digit recalls it");

                // No squad 0 in this arrangement either.
                d.key(SDLK_ESCAPE);
                d.select(*units->tank);
                d.key(SDLK_0);
                d.require(d.selectedUnit() == units->tank, "a bare 0 is still unbound");
            });

            s.at(5, [units](rwe::ScenarioDriver& d) {
                auto commander = d.commander(0);
                d.require(commander.has_value(), "the commander survived to the assign test");
                if (!commander)
                {
                    return;
                }

                // The assign form sets the bit and does not save.
                d.key(SDLK_RETURN);
                d.textInput("+switchalt 0");
                d.key(SDLK_RETURN);
                d.require(!d.switchAltEnabled(), "+SwitchAlt 0 assigns the bit off");
                d.key(SDLK_ESCAPE);
                d.select(*commander);
                d.key(SDLK_2);
                d.require(d.buildPage(*commander) == 1u, "a bare digit opens a page again");

                d.key(SDLK_RETURN);
                d.textInput("+switchalt 1");
                d.key(SDLK_RETURN);
                d.require(d.switchAltEnabled(), "+SwitchAlt 1 assigns the bit on");

                // Leave the saved preference as it was found: the no-argument
                // form is the only one that writes rwe.cfg.
                const auto linesBefore = d.consoleMessageCount();
                d.key(SDLK_RETURN);
                d.textInput("+switchalt");
                d.key(SDLK_RETURN);
                d.require(!d.switchAltEnabled(), "the toggle form turns it off and saves");
                d.require(d.consoleMessageCount() == linesBefore + 1, "and prints the state");
            });
            s.endAt(5);
        });

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

        // #899 / WATER-VIS-003A: the original admits a script effect through
        // the whole-unit display predicate (0x465AC0), so a wholly submerged
        // foreign extractor cannot give itself away with bubbles merely
        // because the seabed under it is lit. The owned twin below proves the
        // shipped script really does emit 259 in this fixture.
        rwe::registerScenario("899-hidden-extractor-bubbles", [](rwe::Scenario& s) {
            auto mex = std::make_shared<std::optional<rwe::UnitId>>();
            s.at(4, [mex](rwe::ScenarioDriver& d) {
                auto pair = d.findWaterFixturePair("ARMCS", "ARMUWMEX", 96.0f);
                d.require(pair.has_value(), "the shipped map has open water for the fixture");
                if (!pair)
                {
                    return;
                }
                // ARMCS: sight 224, no sonar. It lights the seabed beside it
                // without ever granting the sonar contact the waterline rule
                // asks for, which is exactly the reported case.
                auto beacon = d.spawnCompletedAt("ARMCS", 0, pair->first);
                d.require(beacon.has_value(), "a local construction ship provides sight over the water");
                *mex = d.spawnCompletedAt("ARMUWMEX", 1, pair->second);
                d.require(mex->has_value(), "an enemy underwater extractor spawned");
                if (*mex)
                {
                    d.activateUnit(**mex);
                }
            });
            s.at(8, [mex](rwe::ScenarioDriver& d) {
                d.require(mex->has_value(), "the enemy extractor survived its spawn tick");
                if (!*mex)
                {
                    return;
                }
                auto position = d.unitPosition(**mex);
                d.require(position.has_value(), "the enemy extractor is on the map");
                if (position)
                {
                    d.require(d.positionIsVisibleToLocalPlayer(*position),
                        "the seabed under the extractor is in the local player's sight");
                }
                d.require(!d.unitIsVisibleToLocalPlayer(**mex),
                    "a wholly submerged foreign model stays hidden without sonar contact");
            });
            for (unsigned int tick = 8; tick <= 120; tick += 8)
            {
                s.at(tick, [](rwe::ScenarioDriver& d) {
                    d.require(d.bubbleDotCount() == 0, "a hidden foreign extractor leaves no bubbles behind");
                });
            }
            s.endAt(120);
        });

        rwe::registerScenario("899-owned-extractor-bubbles", [](rwe::Scenario& s) {
            auto mex = std::make_shared<std::optional<rwe::UnitId>>();
            auto seen = std::make_shared<bool>(false);
            s.at(4, [mex](rwe::ScenarioDriver& d) {
                auto pair = d.findWaterFixturePair("ARMCS", "ARMUWMEX", 96.0f);
                d.require(pair.has_value(), "the shipped map has open water for the fixture");
                if (!pair)
                {
                    return;
                }
                *mex = d.spawnCompletedAt("ARMUWMEX", 0, pair->second);
                d.require(mex->has_value(), "the player's own underwater extractor spawned");
                if (*mex)
                {
                    d.activateUnit(**mex);
                }
            });
            s.at(8, [mex, seen](rwe::ScenarioDriver& d) {
                d.require(mex->has_value(), "the extractor survived its spawn tick");
                if (!*mex)
                {
                    return;
                }
                d.require(d.unitIsVisibleToLocalPlayer(**mex), "the player's own submerged model is admitted");
                *seen = *seen || d.bubbleDotCount() > 0;
            });
            for (unsigned int tick = 16; tick <= 120; tick += 8)
            {
                s.at(tick, [seen](rwe::ScenarioDriver& d) { *seen = *seen || d.bubbleDotCount() > 0; });
            }
            s.after(120, [seen](rwe::ScenarioDriver& d) {
                d.require(*seen, "the owned extractor's script really emits bubbles here");
            });
            s.endAt(120);
        });

        // #906: the exact developer phrase unlocks debug; the repeat key
        // then reruns the saved `+` line and nothing when it is locked. The
        // original's key is virtual-key 0x5C -- backslash on a US layout.
        // #915: the poster is not a viewport grab. The command has to reach the
        // real off-screen capture path and leave a readable 24-bit PCX behind.
        // Headless mode still builds the GL context the capture needs, so the
        // render itself is exercised here rather than only its arithmetic.
        rwe::registerScenario("915-makeposter-writes-a-poster", [](rwe::Scenario& s) {
            auto posterPath = std::make_shared<fs::path>();
            s.at(1, [posterPath](rwe::ScenarioDriver& d) {
                auto localDataPath = rwe::getLocalDataPath();
                d.require(localDataPath.has_value(), "there is a local data directory");
                if (localDataPath)
                {
                    *posterPath = *localDataPath / "screenshots" / rwe::posterFileName();
                    std::error_code ec;
                    fs::remove(*posterPath, ec);
                }
            });
            s.at(2, [](rwe::ScenarioDriver& d) {
                d.key(SDLK_RETURN);
                d.textInput("+makeposter 3200 2400");
                d.key(SDLK_RETURN);
            });
            s.after(2, [posterPath](rwe::ScenarioDriver& d) {
                d.require(!posterPath->empty(), "the poster path was resolved");
                if (posterPath->empty())
                {
                    return;
                }

                std::ifstream in(*posterPath, std::ios::binary);
                d.require(in.is_open(), "the poster file was written");
                if (!in)
                {
                    return;
                }

                rwe::PcxHeader header{};
                in.read(reinterpret_cast<char*>(&header), sizeof(header));
                d.require(in.gcount() == static_cast<std::streamsize>(sizeof(header)), "the poster has a PCX header");
                d.require(header.manufacturer == 10 && header.version == 5, "the poster is a version-5 PCX");
                d.require(header.numberOfPlanes == 3 && header.bitsPerPixel == 8, "the poster is 24-bit true colour");
                auto width = static_cast<unsigned int>(header.window.xMax) + 1u;
                auto height = static_cast<unsigned int>(header.window.yMax) + 1u;
                d.require(width >= 16 && height >= 16, "the poster has a real image size");
            });
            s.endAt(2);
        });

        rwe::registerScenario("906-developer-phrase-and-repeat", [](rwe::Scenario& s) {
            auto linesBefore = std::make_shared<std::size_t>(0);
            auto unitsBefore = std::make_shared<std::size_t>(0);
            s.at(1, [linesBefore](rwe::ScenarioDriver& d) {
                *linesBefore = d.consoleMessageCount();
                d.key(SDLK_RETURN);
                d.textInput("+Now Film Chris Include Reload Assert");
                d.key(SDLK_RETURN);
                d.require(d.debugCommandsEnabled(), "the exact phrase unlocks debug");
            });
            s.at(2, [linesBefore](rwe::ScenarioDriver& d) {
                // An unknown word is the countable command in RWE: it prints
                // one line per run until #906's tables gain a handler.
                d.key(SDLK_RETURN);
                d.textInput("+RweTestUnknownCommand");
                d.key(SDLK_RETURN);
                d.require(d.consoleMessageCount() == *linesBefore + 1, "the command runs once");
                d.key(SDLK_RETURN);
                d.key(SDLK_BACKSLASH);
                d.require(d.consoleMessageCount() == *linesBefore + 2, "backslash repeats the saved line");
                d.key(SDLK_ESCAPE);
            });
            s.at(3, [](rwe::ScenarioDriver& d) {
                d.key(SDLK_RETURN);
                d.textInput("+Now");
                d.key(SDLK_RETURN);
                d.require(!d.debugCommandsEnabled(), "a bare +Now clears the unlock");
            });
            s.at(4, [](rwe::ScenarioDriver& d) {
                const auto before = d.consoleMessageCount();
                d.key(SDLK_RETURN);
                d.key(SDLK_BACKSLASH);
                d.require(d.consoleMessageCount() == before, "repeat does nothing once debug is off");
                d.key(SDLK_ESCAPE);
            });
            s.at(5, [](rwe::ScenarioDriver& d) {
                // #907's first binding: +NoShake is a local toggle and prints
                // nothing. The switch is what the scenario can see; the shake
                // path reads it when a blast lands.
                d.require(d.screenShakeEnabled(), "shake starts on");
                d.key(SDLK_RETURN);
                d.textInput("+noshake");
                d.key(SDLK_RETURN);
                d.require(!d.screenShakeEnabled(), "the command turns shake off");
                d.key(SDLK_RETURN);
                d.textInput("+NOSHAKE");
                d.key(SDLK_RETURN);
                d.require(d.screenShakeEnabled(), "and a second one turns it back on");
            });
            s.at(6, [](rwe::ScenarioDriver& d) {
                // #907: the option setters' units. The original's +Gamma 12 is
                // 120% brightness; +ScrollSpeed 32 is the base pan speed.
                d.key(SDLK_RETURN);
                d.textInput("+gamma 12");
                d.key(SDLK_RETURN);
                d.require(d.gammaSetting() == 120, "the console gamma maps to 120 percent");
                d.key(SDLK_RETURN);
                d.textInput("+scrollspeed 32");
                d.key(SDLK_RETURN);
                d.require(d.scrollSpeedSetting() == 100, "32 pixels a tick maps to the base pan speed");
            });
            s.at(7, [](rwe::ScenarioDriver& d) {
                // #908: only the categories RWE has. 0 and 1 force the mood,
                // 4 silences; 2 and 3 have no RWE counterpart.
                d.key(SDLK_RETURN);
                d.textInput("+musicmode 1");
                d.key(SDLK_RETURN);
                d.require(d.musicSituation() == 1, "MusicMode 1 forces the battle mood");
                d.key(SDLK_RETURN);
                d.textInput("+musicmode 4");
                d.key(SDLK_RETURN);
                d.require(d.musicSilenced(), "MusicMode 4 silences the chooser");
                d.key(SDLK_RETURN);
                d.textInput("+musicmode 0");
                d.key(SDLK_RETURN);
                d.require(!d.musicSilenced() && d.musicSituation() == 0, "MusicMode 0 returns to building");
                d.key(SDLK_RETURN);
                d.textInput("+sing");
                d.key(SDLK_RETURN);
                d.require(d.singEnabled(), "+Sing toggles the speech waveform");
                d.key(SDLK_RETURN);
                d.textInput("+sing");
                d.key(SDLK_RETURN);
                d.require(!d.singEnabled(), "and off again");
            });
            s.at(8, [](rwe::ScenarioDriver& d) {
                // #907: the render toggles are the original's one-bit
                // switches. Shading is two-state here, not the menu's four.
                const auto antiAlias = d.antiAliasEnabled();
                d.key(SDLK_RETURN);
                d.textInput("+antialias");
                d.key(SDLK_RETURN);
                d.require(d.antiAliasEnabled() != antiAlias, "AntiAlias toggles");
                const auto shadows = d.shadowsEnabled();
                d.key(SDLK_RETURN);
                d.textInput("+shadow");
                d.key(SDLK_RETURN);
                d.require(d.shadowsEnabled() != shadows, "Shadow toggles");
                d.require(d.shadingMode() == 2, "shading starts at buildings-only");
                d.key(SDLK_RETURN);
                d.textInput("+shading");
                d.key(SDLK_RETURN);
                d.require(d.shadingMode() == 0, "Shading turns off");
                d.key(SDLK_RETURN);
                d.textInput("+shading");
                d.key(SDLK_RETURN);
                d.require(d.shadingMode() == 2, "and back to the original's on state");
                const auto vehicleShadows = d.vehicleShadowsEnabled();
                d.key(SDLK_RETURN);
                d.textInput("+tshadow");
                d.key(SDLK_RETURN);
                d.require(d.vehicleShadowsEnabled() != vehicleShadows, "TShadow toggles the mobile-unit bit");
                d.key(SDLK_RETURN);
                d.textInput("+iface 1");
                d.key(SDLK_RETURN);
                d.require(!d.leftClickInterfaceMode(), "IFace 1 selects the right-click interface");
                d.key(SDLK_RETURN);
                d.textInput("+iface 0");
                d.key(SDLK_RETURN);
                d.require(d.leftClickInterfaceMode(), "IFace 0 selects left click");
            });
            s.at(9, [](rwe::ScenarioDriver& d) {
               
                // #910/#912: the replicated stock commands are applied on the
                // next tick through the command stream, so they are asserted
                // a step later.
                d.key(SDLK_RETURN);
                d.textInput("+noenergy 0 100");
                d.key(SDLK_RETURN);
                d.key(SDLK_RETURN);
                d.textInput("+nometal 0 5000");
                d.key(SDLK_RETURN);
                d.key(SDLK_RETURN);
                d.textInput("+atm");
                d.key(SDLK_RETURN);
            });
            s.at(10, [](rwe::ScenarioDriver& d) {
               
                d.require(d.playerMetal(0) == 6000.0f, "NoMetal assigned and ATM added 1000 metal");
                d.require(d.playerEnergy(0) == 1100.0f, "NoEnergy assigned and ATM added 1000 energy");
                // The bare forms target the local player and assign zero.
                d.key(SDLK_RETURN);
                d.textInput("+nometal");
                d.key(SDLK_RETURN);
                d.key(SDLK_RETURN);
                d.textInput("+noenergy");
                d.key(SDLK_RETURN);
            });
            s.at(11, [](rwe::ScenarioDriver& d) {
               
                d.require(d.playerMetal(0) == 0.0f, "the lone token empties the local metal");
                d.require(d.playerEnergy(0) == 0.0f, "the lone token empties the local energy");
                const auto overlay = d.networkOverlayVisible();
                d.key(SDLK_RETURN);
                d.textInput("+bps");
                d.key(SDLK_RETURN);
                d.require(d.networkOverlayVisible() != overlay, "BPS toggles the network overlay");
                const auto screenChat = d.screenChatEnabled();
                d.key(SDLK_RETURN);
                d.textInput("+screenchat");
                d.key(SDLK_RETURN);
                d.require(d.screenChatEnabled() != screenChat, "ScreenChat toggles chat display");
                d.require(!d.clockEnabled(), "the clock starts off");
                d.key(SDLK_RETURN);
                d.textInput("+clock");
                d.key(SDLK_RETURN);
                d.require(d.clockEnabled(), "Clock shows the game time");
                d.key(SDLK_RETURN);
                d.textInput("+clock");
                d.key(SDLK_RETURN);
                d.require(!d.clockEnabled(), "and hides it again");
            });
            s.at(12, [unitsBefore](rwe::ScenarioDriver& d) {
                // #913: the debug-masked default-handler spawn. The phrase was
                // cleared earlier, so it is re-entered first; the cursor is
                // parked away from the start units.
                d.moveMouse(420, 160);
                d.key(SDLK_RETURN);
                d.textInput("+Now Film Chris Include Reload Assert");
                d.key(SDLK_RETURN);
                d.require(d.debugCommandsEnabled(), "the phrase unlocks the spawn route");
                *unitsBefore = d.unitCount();
                d.key(SDLK_RETURN);
                d.textInput("+ARMSOLAR");
                d.key(SDLK_RETURN);
            });
            s.at(13, [unitsBefore](rwe::ScenarioDriver& d) {
                d.require(d.unitCount() == *unitsBefore + 1, "a unit name spawns one unit at the cursor");
            });
            s.endAt(13);
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
        if (scenarioNeedsOpposingSeat(name))
        {
            parameters.players[1] = rwe::parsePlayerInfoFromArg("opponent;Computer;CORE;1");
        }
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
