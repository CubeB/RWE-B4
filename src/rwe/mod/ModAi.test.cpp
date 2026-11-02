#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/mod/ModAiController.h>
#include <rwe/mod/ModAiScript.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/sim_test_util.h>
#include <string>
#include <vector>

/**
 * MOD-09, docs/MODDING-SDK-FINDINGS.md MOD-SDK-010. The scripted seat's
 * lockstep contract: it emits ordinary PlayerUnitCommands, in sorted unit-id
 * order, from its own seeded stream, and a fault is deterministic.
 */
namespace rwe
{
    namespace
    {
        UnitId addUnit(GameSimulation& sim, const std::string& type, PlayerId owner, float x, float z)
        {
            // The CobEnvironment keeps a raw pointer to its script, so every
            // script must outlive the unit. Same arrangement as
            // sim_test_util's addUnitOfType.
            static std::vector<std::shared_ptr<CobScript>> keepAlive;
            auto script = makeEmptyCobScript({"base"});
            keepAlive.push_back(script);
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            const UnitId id(sim.units.emplace(pieces, std::move(env)));
            auto& unit = sim.getUnitState(id);
            unit.unitType = type;
            unit.owner = owner;
            unit.position = SimVector(SimScalar(x), 0_ss, SimScalar(z));
            unit.previousPosition = unit.position;
            unit.hitPoints = 100;
            return id;
        }

        struct World
        {
            GameSimulation sim{makeFlatTerrain(64, 64), 0u, 100, 3000};
            PlayerId owner;
            PlayerId other;
            UnitId own;
            UnitId enemy;

            World()
            {
                owner = addPlayer(sim, "owner");
                other = addPlayer(sim, "other");
                own = addUnit(sim, "ARMCOM", owner, 100.0f, 200.0f);
                enemy = addUnit(sim, "CORCOM", other, 400.0f, 500.0f);
            }
        };

        std::shared_ptr<const ModAiScript> scriptSource(std::string source)
        {
            auto script = std::make_shared<ModAiScript>();
            script->packageId = "com.example.ai";
            script->scriptName = "scripts/main.lua";
            script->source = std::move(source);
            script->options = {{"aggression", 2}};
            script->seed = 20261102u;
            return script;
        }

        std::unique_ptr<ModAiController> controllerFor(
            PlayerId playerId,
            const std::string& source,
            bool idle = false,
            std::uint64_t seed = 42u)
        {
            auto profile = makeProfileForDifficulty(AiDifficulty::Standard);
            profile.idle = idle;
            return std::make_unique<ModAiController>(playerId, scriptSource(source), profile, seed);
        }

        std::string describe(const PlayerCommand& command)
        {
            const auto* unitCommand = std::get_if<PlayerUnitCommand>(&command);
            if (unitCommand == nullptr)
            {
                return "other";
            }
            std::string text = "unit " + std::to_string(unitCommand->unit.value);
            if (std::holds_alternative<PlayerUnitCommand::Stop>(unitCommand->command))
            {
                return text + " stop";
            }
            if (const auto* issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unitCommand->command))
            {
                if (const auto* move = std::get_if<MoveOrder>(&issue->order))
                {
                    return text + " move "
                        + std::to_string(simScalarToFloat(move->destination.x)) + " "
                        + std::to_string(simScalarToFloat(move->destination.z));
                }
                if (const auto* attack = std::get_if<AttackOrder>(&issue->order))
                {
                    if (const auto* target = std::get_if<UnitId>(&attack->target))
                    {
                        return text + " attack " + std::to_string(target->value);
                    }
                }
            }
            return text + " ?";
        }

        std::vector<std::string> describeAll(const std::vector<PlayerCommand>& commands)
        {
            std::vector<std::string> out;
            out.reserve(commands.size());
            for (const auto& command : commands)
            {
                out.push_back(describe(command));
            }
            return out;
        }
    }

    TEST_CASE("a scripted seat reports the lua engine and its own player", "[modai]")
    {
        World world;
        auto controller = controllerFor(world.owner, "function on_tick(tick) end");
        CHECK(controller->engine() == AiEngine::Lua);
        CHECK(controller->getPlayerId() == world.owner);
        CHECK_FALSE(controller->disabled());
    }

    TEST_CASE("two scripted seats from one seed produce byte-identical command sets", "[modai]")
    {
        World world;
        const auto source = R"(
            function on_tick(tick)
                if ai.tick() ~= tick then error("ai.tick disagrees with on_tick") end
                for _, unit in ipairs(ai.my_units()) do
                    ai.cmd_move(unit.id, unit.x + 10, unit.z - 5)
                    ai.cmd_attack(unit.id, unit.id)
                    if ai.rand_below(3) == 0 then
                        ai.cmd_stop(unit.id)
                    end
                end
            end
        )";

        auto first = controllerFor(world.owner, source);
        auto second = controllerFor(world.owner, source);

        std::vector<std::string> firstOutput;
        std::vector<std::string> secondOutput;
        for (unsigned int tick = 1; tick <= 8; ++tick)
        {
            world.sim.gameTime = GameTime(tick);
            std::vector<PlayerCommand> firstTick;
            std::vector<PlayerCommand> secondTick;
            first->tick(world.sim, firstTick);
            second->tick(world.sim, secondTick);
            for (const auto& command : describeAll(firstTick))
            {
                firstOutput.push_back(command);
            }
            for (const auto& command : describeAll(secondTick))
            {
                secondOutput.push_back(command);
            }
        }

        CHECK_FALSE(firstOutput.empty());
        CHECK(firstOutput == secondOutput);
        CHECK_FALSE(first->disabled());
    }

    TEST_CASE("scripted commands leave in sorted unit-id order", "[modai]")
    {
        World world;
        const auto secondOwn = addUnit(world.sim, "ARMCOM", world.owner, 300.0f, 300.0f);
        REQUIRE(secondOwn.value > world.own.value);

        // The script issues in reverse order on purpose: the controller is
        // what guarantees the lockstep order, not the author.
        const auto source = R"(
            function on_tick(tick)
                local units = ai.my_units()
                for i = #units, 1, -1 do
                    ai.cmd_stop(units[i].id)
                end
            end
        )";
        auto controller = controllerFor(world.owner, source);

        std::vector<PlayerCommand> out;
        controller->tick(world.sim, out);

        REQUIRE(out.size() == 2);
        const auto firstId = std::get<PlayerUnitCommand>(out[0]).unit.value;
        const auto secondId = std::get<PlayerUnitCommand>(out[1]).unit.value;
        CHECK(firstId == world.own.value);
        CHECK(secondId == secondOwn.value);
        CHECK(firstId < secondId);
    }

    TEST_CASE("a scripted seat commands its own units and sees no enemy", "[modai]")
    {
        World world;
        const auto source = std::string(R"(
            function on_tick(tick)
                local units = ai.my_units()
                if #units ~= 1 then error("expected one own unit, got " .. #units) end
                if units[1].id ~= )") + std::to_string(world.own.value) + R"( then error("my_units returned another seat's unit") end
                if units[1].type ~= "ARMCOM" then error("wrong unit type") end
                if units[1].health ~= 100 then error("wrong health") end
                ai.cmd_move(units[1].id, 120, 220)
            end
        )";

        auto controller = controllerFor(world.owner, source);
        std::vector<PlayerCommand> out;
        controller->tick(world.sim, out);

        REQUIRE(out.size() == 1);
        const auto& command = std::get<PlayerUnitCommand>(out[0]);
        CHECK(command.unit == world.own);
        const auto* issue = std::get_if<PlayerUnitCommand::IssueOrder>(&command.command);
        REQUIRE(issue != nullptr);
        const auto* move = std::get_if<MoveOrder>(&issue->order);
        REQUIRE(move != nullptr);
        CHECK(simScalarToFloat(move->destination.x) == 120.0f);
        CHECK(simScalarToFloat(move->destination.z) == 220.0f);
        CHECK_FALSE(controller->disabled());
    }

    TEST_CASE("a foreign unit id is refused and disables the seat deterministically", "[modai]")
    {
        World world;
        const auto source = std::string(R"(
            function on_tick(tick)
                ai.cmd_move()") + std::to_string(world.enemy.value) + R"(, 0, 0)
            end
        )";
        auto controller = controllerFor(world.owner, source);

        std::vector<PlayerCommand> first;
        controller->tick(world.sim, first);
        CHECK(first.empty());
        CHECK(controller->disabled());
        CHECK(controller->lastError().find("not owned by this seat") != std::string::npos);

        // Disabled is sticky: the next tick does not re-enter the script.
        std::vector<PlayerCommand> second;
        controller->tick(world.sim, second);
        CHECK(second.empty());
        CHECK(controller->disabled());
        CHECK(controller->lastError().find("not owned by this seat") != std::string::npos);
    }

    TEST_CASE("an idle scripted seat does not think", "[modai]")
    {
        World world;
        const auto source = R"(
            function on_tick(tick)
                ai.log("thought")
                ai.cmd_stop(ai.my_units()[1].id)
            end
        )";
        auto controller = controllerFor(world.owner, source, true);
        CHECK(controller->getProfile().idle);

        std::vector<PlayerCommand> out;
        controller->tick(world.sim, out);

        CHECK(out.empty());
        CHECK(controller->log().empty());
        CHECK_FALSE(controller->disabled());
    }

    TEST_CASE("the script sandbox removes os, io, debug, load and math.random", "[modai]")
    {
        World world;
        // A failed assertion here fails the load, which disables the seat, so
        // the checks below are the test.
        const auto source = R"(
            assert(os == nil, "os is reachable")
            assert(io == nil, "io is reachable")
            assert(package == nil, "package is reachable")
            assert(debug == nil, "debug is reachable")
            assert(load == nil, "load is reachable")
            assert(dofile == nil, "dofile is reachable")
            assert(loadfile == nil, "loadfile is reachable")
            assert(math.random == nil, "math.random is reachable")
            assert(math.randomseed == nil, "math.randomseed is reachable")
            assert(ai.api_version == 1, "wrong api version")
            assert(type(ai.tick) == "function", "ai.tick missing")
            assert(type(ai.my_units) == "function", "ai.my_units missing")
            assert(type(ai.cmd_move) == "function", "ai.cmd_move missing")
            assert(type(ai.cmd_stop) == "function", "ai.cmd_stop missing")
            assert(type(ai.cmd_attack) == "function", "ai.cmd_attack missing")
            assert(type(ai.log) == "function", "ai.log missing")
            assert(ai.options.aggression == 2, "options missing")
            function on_tick(tick) end
        )";

        auto controller = controllerFor(world.owner, source);
        CHECK_FALSE(controller->disabled());

        std::vector<PlayerCommand> out;
        controller->tick(world.sim, out);
        CHECK(out.empty());
    }

    TEST_CASE("a faulty script disables its seat and emits nothing for that tick", "[modai]")
    {
        World world;
        // The stop is appended before the error; a faulting tick did not
        // complete, so none of it is emitted.
        const auto source = R"(
            function on_tick(tick)
                ai.cmd_stop(ai.my_units()[1].id)
                error("boom")
            end
        )";
        auto controller = controllerFor(world.owner, source);

        std::vector<PlayerCommand> out;
        controller->tick(world.sim, out);

        CHECK(out.empty());
        CHECK(controller->disabled());
        CHECK(controller->lastError().find("boom") != std::string::npos);

        std::vector<PlayerCommand> again;
        controller->tick(world.sim, again);
        CHECK(again.empty());
        CHECK(controller->lastError().find("boom") != std::string::npos);
    }

    TEST_CASE("an endless loop is stopped by the instruction budget", "[modai]")
    {
        World world;
        const auto source = R"(
            function on_tick(tick)
                while true do end
            end
        )";
        auto controller = controllerFor(world.owner, source);

        std::vector<PlayerCommand> out;
        controller->tick(world.sim, out);

        CHECK(out.empty());
        CHECK(controller->disabled());
        CHECK(controller->lastError().find("instruction budget exhausted") != std::string::npos);
    }

    TEST_CASE("a script with no on_tick is disabled rather than silently inert", "[modai]")
    {
        World world;
        auto controller = controllerFor(world.owner, "local x = 1");
        CHECK(controller->disabled());
        CHECK(controller->lastError().find("on_tick") != std::string::npos);
    }
}
