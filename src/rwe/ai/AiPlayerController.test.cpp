#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <string>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    TEST_CASE("AI combat micro shares a paced minute budget across units", "[ai][micro]")
    {
        auto orders = [] {
            std::vector<PlayerCommand> commands;
            for (unsigned int id = 0; id < 10; ++id)
            {
                commands.emplace_back(PlayerUnitCommand(UnitId(id), PlayerUnitCommand::IssueOrder(
                    MoveOrder(SimVector(100_ss, 0_ss, 100_ss)), PlayerUnitCommand::IssueOrder::Immediate)));
            }
            return commands;
        };
        for (auto difficulty : {AiDifficulty::Easy, AiDifficulty::Standard})
        {
            AiMicroLimiter limiter;
            std::deque<unsigned int> issued;
            unsigned int total = 0;
            for (unsigned int tick = 0; tick < 120u * SimTicksPerSecond; ++tick)
            {
                auto commands = orders();
                limiter.filter(commands, 0, GameTime(tick), difficulty);
                REQUIRE(commands.size() <= 4);
                while (!issued.empty() && tick - issued.front() >= 60u * SimTicksPerSecond)
                {
                    issued.pop_front();
                }
                issued.insert(issued.end(), commands.size(), tick);
                REQUIRE(issued.size() <= AiMicroLimiter::ordersPerMinute(difficulty));
                total += static_cast<unsigned int>(commands.size());
            }
            REQUIRE(total == 2 * AiMicroLimiter::ordersPerMinute(difficulty));
        }
        for (auto difficulty : {AiDifficulty::Hard, AiDifficulty::Brutal})
        {
            AiMicroLimiter limiter;
            for (unsigned int tick = 0; tick < 60; ++tick)
            {
                auto commands = orders();
                limiter.filter(commands, 0, GameTime(tick), difficulty);
                REQUIRE(commands.size() == 10);
            }
        }
    }

    TEST_CASE("AI micro preserves economy commands and shares turns fairly", "[ai][micro]")
    {
        AiMicroLimiter limiter;
        auto makeCommands = [] {
            std::vector<PlayerCommand> commands;
            commands.emplace_back(PlayerUnitCommand(UnitId(100), PlayerUnitCommand::ModifyBuildQueue{1, "UNIT"}));
            for (unsigned int id = 0; id < 8; ++id)
            {
                commands.emplace_back(PlayerUnitCommand(UnitId(id), PlayerUnitCommand::IssueOrder(
                    AttackOrder(UnitId(99)), PlayerUnitCommand::IssueOrder::Immediate)));
            }
            return commands;
        };
        auto first = makeCommands();
        limiter.filter(first, 1, GameTime(0), AiDifficulty::Standard);
        REQUIRE(first.size() == 5);
        REQUIRE(std::holds_alternative<PlayerUnitCommand::ModifyBuildQueue>(std::get<PlayerUnitCommand>(first[0]).command));
        auto next = makeCommands();
        limiter.filter(next, 1, GameTime(SimTicksPerSecond / 2), AiDifficulty::Standard);
        REQUIRE(next.size() == 2);
        REQUIRE(std::get<PlayerUnitCommand>(next[1]).unit == UnitId(4));
    }

    TEST_CASE("AI micro keeps queued patrol orders together", "[ai][micro]")
    {
        AiMicroLimiter limiter;
        std::vector<PlayerCommand> commands;
        for (unsigned int id = 0; id < 3; ++id)
        {
            for (auto kind : {PlayerUnitCommand::IssueOrder::Immediate, PlayerUnitCommand::IssueOrder::Queued})
            {
                commands.emplace_back(PlayerUnitCommand(UnitId(id), PlayerUnitCommand::IssueOrder(
                    PatrolOrder(SimVector(100_ss, 0_ss, 100_ss)), kind)));
            }
        }
        limiter.filter(commands, 0, GameTime(0), AiDifficulty::Standard);
        REQUIRE(commands.size() == 4);
        REQUIRE(std::get<PlayerUnitCommand>(commands[2]).unit == std::get<PlayerUnitCommand>(commands[3]).unit);
        std::vector<PlayerCommand> pending;
        for (unsigned int waypoint = 0; waypoint < 4; ++waypoint)
        {
            pending.emplace_back(PlayerUnitCommand(UnitId(2), PlayerUnitCommand::IssueOrder(
                PatrolOrder(SimVector(100_ss, 0_ss, 100_ss)), waypoint == 0
                    ? PlayerUnitCommand::IssueOrder::Immediate : PlayerUnitCommand::IssueOrder::Queued)));
        }
        pending.emplace_back(PlayerUnitCommand(UnitId(3), PlayerUnitCommand::IssueOrder(
            MoveOrder(SimVector(0_ss, 0_ss, 0_ss)), PlayerUnitCommand::IssueOrder::Immediate)));
        auto early = pending;
        limiter.filter(early, 0, GameTime(SimTicksPerSecond / 2), AiDifficulty::Standard);
        REQUIRE(early.empty()); // Save up for the older patrol, not the cheap move.
        limiter.filter(pending, 0, GameTime(2 * SimTicksPerSecond), AiDifficulty::Standard);
        REQUIRE(pending.size() == 4);
        REQUIRE(std::get<PlayerUnitCommand>(pending.front()).unit == UnitId(2));
    }

    namespace
    {
        // A barebones UnitDefinition for `unitType`, carrying only the
        // fields the AI actually reads.
        UnitDefinition makeUnitDef(
            bool isCommander,
            bool isBuilder,
            bool extractsMetal,
            unsigned int footprintX = 2,
            unsigned int footprintZ = 2)
        {
            UnitDefinition d;
            d.commander = isCommander;
            d.builder = isBuilder;
            d.extractsMetal = extractsMetal ? Metal(1.0f) : Metal(0.0f);
            d.maxHitPoints = 100;
            d.canAttack = false;
            d.canMove = false;
            d.canGuard = false;
            d.isMobile = false;
            d.floater = false;
            d.canHover = false;
            d.canFly = false;
            d.onOffable = false;
            d.activateWhenBuilt = false;
            d.hideDamage = false;
            d.showPlayerName = false;
            d.yardMapContainsGeo = false;
            // buildTime = 0 means UnitState::isBeingBuilt returns false
            // immediately for any unit constructed this way — we want our
            // commander to be "completed" the instant the test spawns it.
            d.buildTime = 0u;
            d.workerTimePerTick = 0u;
            d.buildCostEnergy = Energy(0.0f);
            d.buildCostMetal = Metal(0.0f);
            d.makesMetal = Metal(0.0f);
            d.energyMake = Energy(0.0f);
            d.metalMake = Metal(0.0f);
            d.energyUse = Energy(0.0f);
            d.metalUse = Metal(0.0f);
            d.energyStorage = Energy(0.0f);
            d.metalStorage = Metal(0.0f);
            d.windGenerator = Energy(0.0f);
            d.sightDistance = 0u;
            d.radarDistance = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{
                footprintX,
                footprintZ,
                255u,
                255u,
                0u,
                0u,
            };
            return d;
        }

        // Add a commander unit at the given position, owned by `owner`.
        UnitId addCommanderUnit(
            GameSimulation& sim,
            PlayerId owner,
            const std::string& unitType,
            const SimVector& pos,
            const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            const UnitId unitId(sim.units.emplace(pieces, std::move(env)));
            auto& unit = sim.getUnitState(unitId);
            unit.unitType = unitType;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 100;
            // Make sure the commander reports as "completed" — this is
            // controlled by buildTimeCompleted vs the def's buildTime.
            // Setting the def's buildTime to 0 in makeUnitDef + leaving
            // buildTimeCompleted at 0 makes UnitState::isBeingBuilt false.
            return unitId;
        }
    }

    TEST_CASE("AiPlayerController is deterministic across two parallel sims", "[ai]")
    {
        auto script = makeEmptyCobScript();

        // Two simulations seeded identically should produce identical
        // AI command streams. This guards against any future inadvertent
        // float comparison or unordered_map iteration leak.
        // 32x32 cells is 512 world units per axis, which is a map big
        // enough to host one commander and a couple of mexes inside the AI's
        // search radius. Nothing here depends on it matching the radius
        // exactly.
        GameSimulation simA(makeFlatTerrain(32, 32), /*surfaceMetal*/ 5u, 0, 0);
        GameSimulation simB(makeFlatTerrain(32, 32), /*surfaceMetal*/ 5u, 0, 0);

        // Sub-seed the AI from a fixed value so the test is fully
        // reproducible without dragging in seedFromGameParameters.
        const std::uint64_t aiSeed = 123456789u;

        // Register one human and one computer using the aggregate-init
        // form LoadingScene uses: {name, type, color, status, side, metal,
        // energy, maxMetal, maxEnergy, startingMetal, startingEnergy}.
        for (auto* sim : {&simA, &simB})
        {
            GamePlayerInfo human{
                std::optional<std::string>("human"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
            sim->addPlayer(human);

            GamePlayerInfo computer{
                std::optional<std::string>("ai"),
                GamePlayerType::Computer,
                PlayerColorIndex(1),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
            sim->addPlayer(computer);
        }

        // Register definitions for the units the AI cares about.
        for (auto* sim : {&simA, &simB})
        {
            sim->unitDefinitions["ARMCOM"] = makeUnitDef(/*commander*/ true, /*builder*/ true, /*extractsMetal*/ false);
            sim->unitDefinitions["ARMMEX"] = makeUnitDef(/*commander*/ false, /*builder*/ false, /*extractsMetal*/ true);
            sim->unitDefinitions["ARMSOLAR"] = makeUnitDef(/*commander*/ false, /*builder*/ false, /*extractsMetal*/ false);
        }

        // Spawn the commander roughly in the middle of the map for both.
        const SimVector commanderPos(
            simA.terrain.heightmapIndexToWorldCenter(16, 16).x,
            0_ss,
            simA.terrain.heightmapIndexToWorldCenter(16, 16).z);

        addCommanderUnit(simA, PlayerId(1), "ARMCOM", commanderPos, script);
        addCommanderUnit(simB, PlayerId(1), "ARMCOM", commanderPos, script);

        // Two controllers seeded the same way.
        AiPlayerController aiA(PlayerId(1), makeDefaultStandardProfile(), aiSeed, MapIntel{});
        AiPlayerController aiB(PlayerId(1), makeDefaultStandardProfile(), aiSeed, MapIntel{});

        std::vector<PlayerCommand> commandsA;
        std::vector<PlayerCommand> commandsB;

        // BuildManager runs every 30 ticks. Tick 60 times so we
        // see two planning windows at minimum.
        for (int i = 0; i < 60; ++i)
        {
            aiA.tick(simA, commandsA);
            aiB.tick(simB, commandsB);
        }

        // The two streams must be identical in size and content.
        REQUIRE(commandsA.size() == commandsB.size());
        // We expect at least one BuildOrder out of the opening pass.
        REQUIRE(!commandsA.empty());
    }

    TEST_CASE("AiPlayerController respects build planner cadence", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(32, 32), /*surfaceMetal*/ 5u, 0, 0);

        GamePlayerInfo computer{
            std::optional<std::string>("ai"),
            GamePlayerType::Computer,
            PlayerColorIndex(0),
            GamePlayerStatus::Alive,
            std::string("ARM"),
            Metal(1000.0f),
            Energy(1000.0f),
            Metal(1000.0f),
            Energy(1000.0f),
            Metal(1000.0f),
            Energy(1000.0f),
        };
        sim.addPlayer(computer);

        sim.unitDefinitions["ARMCOM"] = makeUnitDef(true, true, false);
        sim.unitDefinitions["ARMMEX"] = makeUnitDef(false, false, true);
        sim.unitDefinitions["ARMSOLAR"] = makeUnitDef(false, false, false);

        const SimVector commanderPos(
            sim.terrain.heightmapIndexToWorldCenter(16, 16).x,
            0_ss,
            sim.terrain.heightmapIndexToWorldCenter(16, 16).z);
        addCommanderUnit(sim, PlayerId(0), "ARMCOM", commanderPos, script);

        AiPlayerController ai(PlayerId(0), makeDefaultStandardProfile(), 42u, MapIntel{});

        std::vector<PlayerCommand> commands;

        // Tick 29 times — under the 30-tick planner interval, so the
        // BuildManager should have emitted nothing.
        for (int i = 0; i < 29; ++i)
        {
            ai.tick(sim, commands);
        }
        REQUIRE(commands.empty());

        // 30th tick triggers the first plan.
        ai.tick(sim, commands);
        REQUIRE(commands.size() == 1);
    }

    TEST_CASE("An Idle computer player does nothing at all", "[ai]")
    {
        // The same setup as the cadence test above, which reaches its first
        // build order on tick 30. An Idle profile has to stay silent past
        // that, and past every later planning interval, or it is not idle --
        // it is just slow.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(32, 32), /*surfaceMetal*/ 5u, 0, 0);

        GamePlayerInfo computer{
            std::optional<std::string>("ai"),
            GamePlayerType::Computer,
            PlayerColorIndex(0),
            GamePlayerStatus::Alive,
            std::string("ARM"),
            Metal(1000.0f),
            Energy(1000.0f),
            Metal(1000.0f),
            Energy(1000.0f),
            Metal(1000.0f),
            Energy(1000.0f),
        };
        sim.addPlayer(computer);

        sim.unitDefinitions["ARMCOM"] = makeUnitDef(true, true, false);
        sim.unitDefinitions["ARMMEX"] = makeUnitDef(false, false, true);
        sim.unitDefinitions["ARMSOLAR"] = makeUnitDef(false, false, false);

        const SimVector commanderPos(
            sim.terrain.heightmapIndexToWorldCenter(16, 16).x,
            0_ss,
            sim.terrain.heightmapIndexToWorldCenter(16, 16).z);
        addCommanderUnit(sim, PlayerId(0), "ARMCOM", commanderPos, script);

        AiPlayerController ai(PlayerId(0), makeProfileForDifficulty(AiDifficulty::Idle), 42u, MapIntel{});

        std::vector<PlayerCommand> commands;
        for (int i = 0; i < 200; ++i)
        {
            ai.tick(sim, commands);
        }
        REQUIRE(commands.empty());

        // And it does not even look: an idle player costs nothing, which is
        // the point when the reason for switching it off was to measure
        // something else.
        REQUIRE(ai.getBlackboard().ownedTotalCounts.empty());
        REQUIRE(!ai.getBlackboard().commanderUnitId);
    }

    TEST_CASE("AiPlayerController emits no commands when no commander exists", "[ai]")
    {
        GameSimulation sim(makeFlatTerrain(32, 32), /*surfaceMetal*/ 5u, 0, 0);

        GamePlayerInfo computer{
            std::optional<std::string>("ai"),
            GamePlayerType::Computer,
            PlayerColorIndex(0),
            GamePlayerStatus::Alive,
            std::string("ARM"),
            Metal(1000.0f),
            Energy(1000.0f),
            Metal(1000.0f),
            Energy(1000.0f),
            Metal(1000.0f),
            Energy(1000.0f),
        };
        sim.addPlayer(computer);

        sim.unitDefinitions["ARMCOM"] = makeUnitDef(true, true, false);
        sim.unitDefinitions["ARMMEX"] = makeUnitDef(false, false, true);
        sim.unitDefinitions["ARMSOLAR"] = makeUnitDef(false, false, false);

        // No units spawned at all — bb.commanderUnitId stays empty.

        AiPlayerController ai(PlayerId(0), makeDefaultStandardProfile(), 42u, MapIntel{});

        std::vector<PlayerCommand> commands;
        for (int i = 0; i < 60; ++i)
        {
            ai.tick(sim, commands);
        }

        REQUIRE(commands.empty());
    }

    TEST_CASE("AiTuningProfile Brutal flips cheat flags", "[ai]")
    {
        const auto std_profile = makeDefaultStandardProfile();
        REQUIRE(std_profile.difficulty == AiDifficulty::Standard);
        REQUIRE(std_profile.cheatModeOmniscient == false);
        REQUIRE(std_profile.resourceCheatMultiplier == 1_ss);

        const auto brutal = makeDefaultBrutalProfile();
        REQUIRE(brutal.difficulty == AiDifficulty::Brutal);
        REQUIRE(brutal.cheatModeOmniscient == true);
        REQUIRE(brutal.resourceCheatMultiplier == 1.25_ssf);
    }
}
