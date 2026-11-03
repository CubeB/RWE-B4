#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <rwe/mod/ModTransform.h>
#include <rwe/presentation/PresentationRuntime.h>
#include <rwe/presentation/PresentationScript.h>
#include <rwe/presentation/PresentationView.h>
#include <rwe/setup/ModManifest.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        struct PresentationWorld
        {
            GameSimulation sim{makeFlatTerrain(64, 64), 0u, 0, 0};
            PlayerId viewer;
            PlayerId enemy;
            UnitId own;
            UnitId hidden;

            PresentationWorld()
            {
                viewer = addPlayer(sim, "viewer");
                enemy = addPlayer(sim, "enemy");

                UnitDefinition definition{};
                definition.unitName = "Test Unit";
                definition.objectName = "model";
                definition.maxHitPoints = 100;
                definition.isMobile = true;
                definition.canMove = true;
                definition.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2, 2, 255u, 255u, 0u, 0u};
                sim.unitDefinitions["TEST"] = definition;
                sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, {});

                own = addUnitOfType(sim, "TEST", viewer, SimVector(100_ss, 0_ss, 100_ss), makeEmptyCobScript());
                hidden = addUnitOfType(sim, "TEST", enemy, SimVector(900_ss, 0_ss, 900_ss), makeEmptyCobScript());
            }

            PlayerView view() const
            {
                return PlayerView(sim, viewer, sim.playerVisibility.at(viewer.value));
            }
        };

        PresentationPackage package(const std::string& source, const std::string& id = "test.presentation")
        {
            PresentationPackage result;
            result.packageId = id;
            result.scriptName = id + "/presentation.lua";
            result.source = source;
            return result;
        }
    }

    TEST_CASE("[presentation] a frame draws through the draw list and never moves the sync hash", "[presentation][modsdk]")
    {
        PresentationWorld world;
        const auto before = computeHashOf(world.sim);

        auto runtime = PresentationRuntime({package(R"(
            function on_frame(dt)
                local units = rwe.my_units()
                local resources = rwe.resources()
                rwe.draw_rect(10, 20, 100, 30, {r = 20, g = 40, b = 60, a = 200})
                rwe.draw_text(12, 24, string.format("Metal: %d", resources.metal))
                rwe.draw_bar(12, 30, 90, 4, resources.metal / resources.max_metal, {r = 255})
                rwe.draw_line(0, 0, 10, 10, {r = 255, g = 255, b = 255})
                rwe.submit_move({units[1].id}, 200, 200)
            end
        )")});

        auto view = world.view();
        PresentationFrameInput input;
        input.uiWidth = 640.0f;
        input.uiHeight = 480.0f;
        input.deltaSeconds = 0.033f;
        runtime.beginFrame(view, input);

        REQUIRE(runtime.disabledCount() == 0);
        REQUIRE(runtime.drawList().commands.size() == 4);
        REQUIRE(runtime.drawList().commands[0].kind == PresentationDrawCommand::Kind::Rect);
        REQUIRE(runtime.drawList().commands[1].kind == PresentationDrawCommand::Kind::Text);
        REQUIRE(runtime.drawList().commands[1].text == "Metal: 1000");
        REQUIRE(runtime.drawList().commands[2].kind == PresentationDrawCommand::Kind::Bar);
        REQUIRE(runtime.drawList().commands[3].kind == PresentationDrawCommand::Kind::Line);

        auto commands = runtime.takeCommands();
        REQUIRE(commands.size() == 1);
        REQUIRE(commands.front().kind == PresentationCommand::Kind::Move);
        REQUIRE(commands.front().units.size() == 1);
        REQUIRE(commands.front().units.front() == world.own);
        REQUIRE(commands.front().position.x == SimScalar(200.0f));
        REQUIRE(commands.front().position.z == SimScalar(200.0f));

        REQUIRE(computeHashOf(world.sim) == before);
    }

    TEST_CASE("[presentation] a hidden unit cannot be ordered by id and a contact carries no identity", "[presentation][modsdk]")
    {
        PresentationWorld world;
        world.sim.playerVisibility.at(world.viewer.value).radarContacts.insert(world.hidden);

        auto runtime = PresentationRuntime({package(R"(
            function on_frame(dt)
                local hidden = )" + std::to_string(world.hidden.value) + R"(
                rwe.submit_move({hidden}, 10, 10)
                local contacts = rwe.contacts()
                local hidden_again = contacts[1] and contacts[1].id or -1
                assert(rwe.unit(hidden) == nil)
                assert(rwe.unit(hidden_again) == nil)
                assert(contacts[1].type == nil)
                assert(contacts[1].hit_points == nil)
            end
        )")});

        auto view = world.view();
        runtime.beginFrame(view, PresentationFrameInput{});
        REQUIRE(runtime.disabledCount() == 0);
        REQUIRE(runtime.takeCommands().empty());
    }

    TEST_CASE("[presentation] the sandbox has no files, clock, os or unsandboxed randomness", "[presentation][modsdk]")
    {
        PresentationWorld world;
        auto runtime = PresentationRuntime({package(R"(
            function on_frame(dt)
                assert(io == nil)
                assert(os == nil)
                assert(dofile == nil)
                assert(loadfile == nil)
                assert(load == nil)
                assert(math.random == nil)
                assert(rwe.api_version == 1)
                assert(rwe.units() ~= nil)
            end
        )")});

        auto view = world.view();
        runtime.beginFrame(view, PresentationFrameInput{});
        REQUIRE(runtime.disabledCount() == 0);
    }

    TEST_CASE("[presentation] a mod that faults is disabled and a failed reload keeps the working HUD", "[presentation][modsdk]")
    {
        PresentationWorld world;
        auto good = package(R"(
            function on_frame(dt)
                rwe.draw_text(0, 0, "ok")
            end
        )", "test.good");

        auto runtime = PresentationRuntime({good});
        auto view = world.view();
        runtime.beginFrame(view, PresentationFrameInput{});
        REQUIRE(runtime.drawList().commands.size() == 1);

        runtime.beginFrame(view, PresentationFrameInput{});
        REQUIRE(runtime.drawList().commands.size() == 1);

        // A replacement that fails to parse must not replace the working one.
        auto broken = package("function on_frame(dt) this is not lua end", "test.broken");
        REQUIRE_FALSE(runtime.reload({broken}));
        runtime.beginFrame(view, PresentationFrameInput{});
        REQUIRE(runtime.disabledCount() == 0);
        REQUIRE(runtime.drawList().commands.size() == 1);

        // A replacement that faults at load is also refused.
        auto faulting = package("error('no')", "test.faulting");
        REQUIRE_FALSE(runtime.reload({faulting}));

        // A good replacement takes over.
        auto replacement = package(R"(
            function on_frame(dt)
                rwe.draw_text(0, 0, "new")
                rwe.draw_text(0, 10, "new")
            end
        )", "test.replacement");
        REQUIRE(runtime.reload({replacement}));
        runtime.beginFrame(view, PresentationFrameInput{});
        REQUIRE(runtime.drawList().commands.size() == 2);
        REQUIRE(runtime.drawList().commands.front().text == "new");
    }

    TEST_CASE("[presentation] the shipped HUD example resolves, draws and reloads", "[presentation][modsdk]")
    {
        namespace fs = std::filesystem;
        const auto root = fs::path(RWE_SOURCE_DIR) / "examples" / "mod-presentation-hud";
        std::ifstream manifestFile(root / "rwe_mod.json", std::ios::binary);
        REQUIRE(manifestFile);
        std::string manifestText((std::istreambuf_iterator<char>(manifestFile)), std::istreambuf_iterator<char>());

        const auto parsed = modPackageFromJson(nlohmann::json::parse(manifestText), manifestText, root.string());
        REQUIRE(parsed);
        REQUIRE(parsed.get().manifest.kind == ModKind::Presentation);

        const auto set = resolveModSet({parsed.get()}, {}, {});
        REQUIRE(set);
        REQUIRE(set.get().mods.size() == 1);

        ModLaunchContent content;
        content.identity = set.get().identity;
        ModLaunchPackage launch;
        launch.id = parsed.get().manifest.id;
        launch.kind = modKindText(parsed.get().manifest.kind);
        launch.scriptName = launch.id + "/" + *parsed.get().manifest.entry;
        launch.scriptPath = (root / *parsed.get().manifest.entry).string();
        content.packages.push_back(std::move(launch));

        auto packages = resolvedPresentationPackages(&content);
        REQUIRE(packages.size() == 1);
        REQUIRE(packages.front().packageId == "com.example.presentationhud");

        PresentationWorld world;
        auto runtime = PresentationRuntime(packages);
        auto view = world.view();
        runtime.beginFrame(view, PresentationFrameInput{});
        REQUIRE(runtime.disabledCount() == 0);
        REQUIRE(runtime.drawList().commands.size() >= 5);
        REQUIRE(runtime.takeCommands().empty());

        // The developer reload path rebuilds the same package and keeps it.
        REQUIRE(runtime.reload(resolvedPresentationPackages(&content)));
        runtime.beginFrame(view, PresentationFrameInput{});
        REQUIRE(runtime.disabledCount() == 0);
        REQUIRE(runtime.drawList().commands.size() >= 5);
    }

    TEST_CASE("[presentation] input hooks report whether they consumed the event", "[presentation][modsdk]")
    {
        PresentationWorld world;
        auto runtime = PresentationRuntime({package(R"(
            function on_mouse_down(x, y, button)
                return button == 1 and x > 100
            end
            function on_key_down(key)
                return key == 42
            end
        )")});

        auto view = world.view();
        PresentationFrameInput input;
        input.mouseX = 150.0f;
        input.mouseY = 20.0f;

        REQUIRE(runtime.mouseDown(view, input, 1));
        REQUIRE_FALSE(runtime.mouseDown(view, input, 2));
        input.mouseX = 50.0f;
        REQUIRE_FALSE(runtime.mouseDown(view, input, 1));
        REQUIRE(runtime.keyDown(view, input, 42));
        REQUIRE_FALSE(runtime.keyDown(view, input, 7));
    }
}
