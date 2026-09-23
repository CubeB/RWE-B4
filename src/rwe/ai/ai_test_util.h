#pragma once

#include <array>
#include <algorithm>
#include <rwe/ai/AiBuildTree.h>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/MapIntel.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>
#include <rwe/sim/sim_test_util.h>

/**
 * Fixtures the AI behaviour tests share.
 *
 * Subject-specific helpers live with their subject: countOrders in the army
 * tests, makeTwoShoresTerrain in the naval ones, siteTestBlackboard in the
 * siting ones.
 */
namespace rwe
{
    inline PlayerId addPlayer(GameSimulation& sim, const std::string& name, GamePlayerType type, const std::string& side)
    {
        GamePlayerInfo p{
            std::optional<std::string>(name),
            type,
            PlayerColorIndex(0),
            GamePlayerStatus::Alive,
            side,
            Metal(1000.0f),
            Energy(1000.0f),
            Metal(1000.0f),
            Energy(1000.0f),
            Metal(1000.0f),
            Energy(1000.0f),
        };
        return sim.addPlayer(p);
    }

    inline UnitDefinition makeDef(bool commander, bool builder, bool mobile, const std::string& weapon, unsigned int sight)
    {
        UnitDefinition d{};
        d.commander = commander;
        d.builder = builder;
        d.isMobile = mobile;
        d.canMove = mobile;
        d.canAttack = !weapon.empty();
        d.weapon1 = weapon;
        d.maxHitPoints = 100;
        d.buildTime = 100u;
        d.buildCostMetal = Metal(100.0f);
        d.buildCostEnergy = Energy(100.0f);
        d.sightDistance = sight;
        d.maxVelocity = 2_ss;
        d.acceleration = 1_ss;
        d.brakeRate = 1_ss;
        d.turnRate = 1000_ss;
        d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
        return d;
    }

    inline void defineWorld(GameSimulation& sim)
    {
        WeaponDefinition laser{};
        laser.maxRange = 200_ss;
        laser.reloadTime = 1_ss;
        laser.burst = 1;
        // "DEFAULT" upper case, as the loader stores it and the
        // simulation looks it up (Projectile.cpp).
        laser.damage["DEFAULT"] = 30u;
        sim.weaponDefinitions["LASER"] = laser;

        sim.unitDefinitions["ARMCOM"] = makeDef(true, true, true, "", 300u);
        sim.unitDefinitions["ARMMEX"] = makeDef(false, false, false, "", 50u);
        sim.unitDefinitions["ARMSOLAR"] = makeDef(false, false, false, "", 50u);
        sim.unitDefinitions["ARMLAB"] = makeDef(false, true, false, "", 100u);
        sim.unitDefinitions["ARMCK"] = makeDef(false, true, true, "", 100u);
        sim.unitDefinitions["ARMPW"] = makeDef(false, false, true, "LASER", 200u);
        sim.unitDefinitions["ARMROCK"] = makeDef(false, false, true, "LASER", 200u);
        sim.unitDefinitions["ARMLLT"] = makeDef(false, false, false, "LASER", 200u);
        sim.unitDefinitions["ARMRAD"] = makeDef(false, false, false, "", 100u);
        // Anti-air: a missile tower a constructor puts up, and the
        // level-1 kbot the first lab can already build.
        sim.unitDefinitions["ARMRL"] = makeDef(false, false, false, "LASER", 200u);
        sim.unitDefinitions["ARMJETH"] = makeDef(false, false, true, "LASER", 200u);
        sim.unitDefinitions["CORCOM"] = makeDef(true, true, true, "", 300u);
        sim.unitDefinitions["CORSOLAR"] = makeDef(false, false, false, "", 50u);

        // Eyes and lift: a scout plane, an air transport and the plants that make them.
        auto peeper = makeDef(false, false, true, "", 400u);
        peeper.canFly = true;
        peeper.maxVelocity = 6_ss;
        sim.unitDefinitions["ARMPEEP"] = peeper;
        auto atlas = makeDef(false, false, true, "", 100u);
        atlas.canFly = true;
        atlas.transportCapacity = 1;
        atlas.transportSize = 3;
        sim.unitDefinitions["ARMATLAS"] = atlas;
        sim.unitDefinitions["ARMAP"] = makeDef(false, true, false, "", 100u);
        sim.unitDefinitions["ARMVP"] = makeDef(false, true, false, "", 100u);
        sim.unitDefinitions["ARMFAV"] = makeDef(false, false, true, "LASER", 300u);
        sim.unitDefinitions["ARMFLASH"] = makeDef(false, false, true, "LASER", 200u);

        // Level two. The advanced lab is a factory, its constructor is a
        // mobile builder, and the rest are what that constructor puts up.
        sim.unitDefinitions["ARMALAB"] = makeDef(false, true, false, "", 100u);
        sim.unitDefinitions["ARMACK"] = makeDef(false, true, true, "", 100u);
        // Worth teching for: the same price as a raider and four times
        // the hit points, which is the shape of Core's Can against an
        // A.K. and the reason teching is a per-side question at all.
        auto zeus = makeDef(false, false, true, "LASER", 200u);
        zeus.maxHitPoints = 400;
        sim.unitDefinitions["ARMZEUS"] = zeus;
        sim.unitDefinitions["ARMHLT"] = makeDef(false, false, false, "LASER", 200u);
        sim.unitDefinitions["ARMGUARD"] = makeDef(false, false, false, "LASER", 200u);
        sim.unitDefinitions["ARMARAD"] = makeDef(false, false, false, "", 200u);
        sim.unitDefinitions["ARMMOHO"] = makeDef(false, false, false, "", 50u);
        sim.unitDefinitions["ARMFUS"] = makeDef(false, false, false, "", 50u);

        // Naval: real shipped values (docs/ai-architecture-proposal.md
        // S:13.2). The shipyard floats IN the water it needs rather
        // than standing beside it -- 8x8, MinWaterDepth=30 -- and the
        // scout ship and destroyer are ordinary armed hulls with their
        // own, much shallower draughts.
        auto armsy = makeDef(false, true, false, "", 200u);
        armsy.movementCollisionInfo = UnitDefinition::AdHocMovementClass{8u, 8u, 255u, 255u, 30u, 255u};
        armsy.buildCostMetal = Metal(615.0f);
        sim.unitDefinitions["ARMSY"] = armsy;

        auto armpt = makeDef(false, false, true, "LASER", 300u);
        armpt.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 6u, 255u};
        armpt.buildCostMetal = Metal(100.0f);
        sim.unitDefinitions["ARMPT"] = armpt;

        auto armroy = makeDef(false, false, true, "LASER", 300u);
        armroy.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 12u, 255u};
        armroy.buildCostMetal = Metal(898.0f);
        sim.unitDefinitions["ARMROY"] = armroy;
    }

    /**
     * The real tech tree in miniature: the commander's pages stop at the
     * level-one plants, the construction kbot reaches the tech step and
     * the towers, and only the advanced constructor reaches the rest.
     */
    inline AiBuildTree makeBuildTree()
    {
        AiBuildTree tree;
        // The water structures are on the COMMANDER's pages and nowhere
        // else that matters here: ARMCOM3 carries the tidal generator and
        // sonar, ARMCOM4 the torpedo launcher. ARMCK's three pages carry
        // none of the three, and neither the v3.1 patch nor the expansion
        // ships an ARMCK menu that adds them -- so the omission from
        // ARMCK below is the shipped data, not an oversight.
        tree.buildableBy["ARMCOM"] = {"ARMSOLAR", "ARMMEX", "ARMLAB", "ARMVP", "ARMAP", "ARMLLT", "ARMRAD", "ARMMAKR", "ARMSY",
            "ARMTIDE", "ARMSONAR", "ARMTL", "ARMUWMEX", "ARMFMKR"};
        tree.buildableBy["ARMCK"] = {"ARMSOLAR", "ARMMEX", "ARMLAB", "ARMVP", "ARMAP", "ARMLLT", "ARMRAD", "ARMMAKR",
            "ARMALAB", "ARMHLT", "ARMGUARD", "ARMRL", "ARMSY"};
        tree.buildableBy["ARMACK"] = {"ARMLAB", "ARMARAD", "ARMFUS", "ARMMOHO"};
        return tree;
    }

    /**
     * Two banks of land either side of a channel too deep for a kbot, running
     * north to south. The channel is 8 heightmap tiles wide, world x -64..64
     * -- narrow on purpose: every AI test here works in footprints labelled by
     * centre and needs only a barrier with banks far enough apart to stand a
     * base on. The sea-transport tests in TransportManager.test.cpp use their
     * own makeWideChannelTerrain instead, because a hull's footprint is
     * labelled by its top-left corner and this width leaves a 6x6 ship almost
     * no valid column.
     */
    inline MapTerrain makeChannelTerrain()
    {
        Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(60));
        for (int y = 0; y < 64; ++y)
        {
            for (int x = 28; x < 36; ++x)
            {
                heights.set(x, y, static_cast<unsigned char>(0));
            }
        }
        return MapTerrain(std::move(heights), 30_ss);
    }

    /**
     * Mostly open water 60 deep -- comfortably past NavalShipyardMinWaterDepth
     * -- with a dry strip along the west edge (heightmap x in [0, 10)) for
     * the base to stand on. Heightmap width 64 and HeightTileWidthInWorldUnits
     * 16 puts world x 0 at tile 32, so the shore is at world x -352 and
     * everything east of it is water.
     */
    inline MapTerrain makeWaterMapTerrain()
    {
        Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
        for (int y = 0; y < 64; ++y)
        {
            for (int x = 0; x < 10; ++x)
            {
                heights.set(x, y, static_cast<unsigned char>(90));
            }
            // A shelf too shallow for a shipyard (depth 20, under
            // NavalShipyardMinWaterDepth=30) but still water for
            // waterFraction/character purposes -- so a valid nomination
            // has to have skipped it, not just have skipped the dry land.
            for (int x = 10; x < 14; ++x)
            {
                heights.set(x, y, static_cast<unsigned char>(40));
            }
        }
        return MapTerrain(std::move(heights), 60_ss);
    }

    template <typename Order>
    inline std::vector<Order> ordersFor(const std::vector<PlayerCommand>& commands, UnitId unit)
    {
        std::vector<Order> found;
        for (const auto& c : commands)
        {
            auto unitCommand = std::get_if<PlayerUnitCommand>(&c);
            if (!unitCommand || unitCommand->unit != unit)
            {
                continue;
            }
            if (auto issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unitCommand->command))
            {
                if (auto order = std::get_if<Order>(&issue->order))
                {
                    found.push_back(*order);
                }
            }
        }
        return found;
    }

    inline UnitId addUnit(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
    {
        auto env = std::make_unique<CobEnvironment>(script.get());
        std::vector<UnitMesh> pieces;
        const UnitId unitId(sim.units.emplace(pieces, std::move(env)));
        auto& unit = sim.getUnitState(unitId);
        unit.unitType = type;
        unit.owner = owner;
        unit.position = pos;
        unit.previousPosition = pos;
        unit.hitPoints = 100;
        unit.buildTimeCompleted = sim.unitDefinitions.at(type).buildTime;
        if (sim.unitDefinitions.at(type).canFly)
        {
            UnitPhysicsInfoAir air;
            air.movementState = AirMovementStateFlying();
            unit.physics = air;
        }
        return unitId;
    }

    /** Build orders naming a particular unit type. */
    inline int countOrdersFor(const std::vector<PlayerCommand>& commands, const std::string& unitType)
    {
        int n = 0;
        for (const auto& c : commands)
        {
            auto unitCommand = std::get_if<PlayerUnitCommand>(&c);
            if (!unitCommand)
            {
                continue;
            }
            auto issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unitCommand->command);
            if (!issue)
            {
                continue;
            }
            if (auto build = std::get_if<BuildOrder>(&issue->order); build && build->unitType == unitType)
            {
                ++n;
            }
        }
        return n;
    }

    inline std::vector<std::string> buildOrderTypes(const std::vector<PlayerCommand>& commands)
    {
        std::vector<std::string> types;
        for (const auto& c : commands)
        {
            auto unitCommand = std::get_if<PlayerUnitCommand>(&c);
            if (!unitCommand)
            {
                continue;
            }
            if (auto issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unitCommand->command))
            {
                if (auto build = std::get_if<BuildOrder>(&issue->order))
                {
                    types.push_back(build->unitType);
                }
            }
        }
        return types;
    }

    inline int countQueueCommands(const std::vector<PlayerCommand>& commands, const std::string& type)
    {
        int n = 0;
        for (const auto& c : commands)
        {
            auto unitCommand = std::get_if<PlayerUnitCommand>(&c);
            if (!unitCommand)
            {
                continue;
            }
            if (auto q = std::get_if<PlayerUnitCommand::ModifyBuildQueue>(&unitCommand->command); q && q->unitType == type)
            {
                ++n;
            }
        }
        return n;
    }

    inline void runTicks(GameSimulation& sim, AiPlayerController& ai, int ticks, std::vector<PlayerCommand>& out)
    {
        for (int i = 0; i < ticks; ++i)
        {
            sim.tick();
            ai.tick(sim, out);
        }
    }

    inline SimScalar flatDistanceBetween(const SimVector& a, const SimVector& b)
    {
        auto dx = a.x - b.x;
        auto dz = a.z - b.z;
        return rweSqrt((dx * dx) + (dz * dz));
    }

    /** The one build order in the commands, and where it was for. */
    inline BuildOrder theBuildOrder(const std::vector<PlayerCommand>& commands, const std::string& unitType)
    {
        std::vector<BuildOrder> found;
        for (const auto& c : commands)
        {
            auto unitCommand = std::get_if<PlayerUnitCommand>(&c);
            if (!unitCommand)
            {
                continue;
            }
            if (auto issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unitCommand->command))
            {
                if (auto build = std::get_if<BuildOrder>(&issue->order))
                {
                    found.push_back(*build);
                }
            }
        }
        REQUIRE(found.size() == 1);
        REQUIRE(found.front().unitType == unitType);
        return found.front();
    }

    /**
     * A base past its opening, with its radar, standing off-centre at
     * x = -300 so that "towards the map centre" is +x. Everything is
     * within a hundred of the anchor, inside one tower's reach.
     */
    inline void layOutBase(GameSimulation& sim, PlayerId ai, const std::shared_ptr<CobScript>& script, const SimVector& anchor)
    {
        addUnit(sim, "ARMCOM", ai, anchor, script);
        addUnit(sim, "ARMLAB", ai, anchor + SimVector(0_ss, 0_ss, -80_ss), script);
        addUnit(sim, "ARMRAD", ai, anchor + SimVector(-80_ss, 0_ss, -80_ss), script);
        for (int i = 0; i < 4; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, anchor + SimVector(SimScalar(-90.0f + i * 30.0f), 0_ss, 80_ss), script);
        }
        for (int i = 0; i < 3; ++i)
        {
            addUnit(sim, "ARMMEX", ai, anchor + SimVector(-90_ss, 0_ss, SimScalar(-40.0f + i * 40.0f)), script);
        }
    }

    /**
     * A base built out as far as its two towers, the commander walking
     * off so the construction kbot is the builder the planner has, and
     * every builder able to repair, as every shipped one is.
     */
    struct RepairBase
    {
        std::shared_ptr<CobScript> script = makeEmptyCobScript();
        GameSimulation sim{makeFlatTerrain(64, 64), 0u, 0, 0};
        PlayerId ai;
        UnitId commanderId{0};
        UnitId kbotId{0};
        UnitId labId{0};
        UnitId towerId{0};
        UnitId solarId{0};

        RepairBase()
            : ai(addPlayer(sim, "ai", GamePlayerType::Computer, "ARM"))
        {
            defineWorld(sim);
            sim.unitDefinitions["ARMCK"].canReclamate = true;
            sim.unitDefinitions["ARMCOM"].canReclamate = true;
            commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(-400_ss, 0_ss, 400_ss)));
            kbotId = addUnit(sim, "ARMCK", ai, SimVector(-60_ss, 0_ss, 60_ss), script);
            for (int i = 0; i < 4; ++i)
            {
                auto id = addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(-200.0f + i * 40.0f), 0_ss, 150_ss), script);
                if (i == 0)
                {
                    solarId = id;
                }
            }
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMMEX", ai, SimVector(-200_ss, 0_ss, SimScalar(-40.0f + i * 40.0f)), script);
            }
            labId = addUnit(sim, "ARMLAB", ai, SimVector(0_ss, 0_ss, 200_ss), script);
            addUnit(sim, "ARMVP", ai, SimVector(-120_ss, 0_ss, 220_ss), script);
            addUnit(sim, "ARMRAD", ai, SimVector(-100_ss, 0_ss, 100_ss), script);
            towerId = addUnit(sim, "ARMLLT", ai, SimVector(100_ss, 0_ss, -100_ss), script);
            addUnit(sim, "ARMLLT", ai, SimVector(-100_ss, 0_ss, -100_ss), script);
            for (auto& [id, unit] : sim.units)
            {
                unit.hitPoints = sim.unitDefinitions.at(unit.unitType).maxHitPoints;
            }
        }

        /** What the kbot is first told to repair, over the second the planner needs. */
        std::optional<UnitId> firstRepair(const AiTuningProfile& profile)
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto repairs = ordersFor<RepairOrder>(commands, kbotId);
            if (repairs.empty())
            {
                return std::nullopt;
            }
            return repairs.front().target;
        }
    };

    /**
     * A base built out to the point where the plan has nothing basic left
     * to want -- the opening quotas, the lab, one construction kbot --
     * and eight deposits of 2x2 cells lying free around it, at the cell
     * corners given (the 64-wide test map is centred on the middle, so
     * cell c is at world (c - 32) * 16 + 8).
     */
    struct ExpansionWorld
    {
        GameSimulation sim{makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0};
        std::shared_ptr<CobScript> script = makeEmptyCobScript();
        PlayerId human;
        PlayerId ai;
        UnitId commanderId;
        UnitId constructorId;

        static constexpr std::array<std::pair<int, int>, 8> DepositCells{{{8, 8}, {14, 8}, {20, 8}, {8, 50}, {14, 50}, {50, 8}, {50, 14}, {50, 50}}};

        static SimVector depositCentre(std::size_t i)
        {
            return SimVector(SimScalar(static_cast<float>((DepositCells[i].first - 32) * 16 + 16)), 0_ss, SimScalar(static_cast<float>((DepositCells[i].second - 32) * 16 + 16)));
        }

        ExpansionWorld()
        {
            human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
            ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
            defineWorld(sim);
            // What makes an extractor one, as ARMMEX.FBI has it.
            sim.unitDefinitions["ARMMEX"].extractsMetal = Metal(0.001f);
            for (const auto& [cx, cy] : DepositCells)
            {
                for (int y = cy; y < cy + 2; ++y)
                {
                    for (int x = cx; x < cx + 2; ++x)
                    {
                        sim.metalGrid.set(x, y, static_cast<unsigned char>(200));
                    }
                }
            }
            commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
            for (int i = 0; i < 4; ++i)
            {
                addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(100.0f + i * 40.0f), 0_ss, 0_ss), script);
            }
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMMEX", ai, SimVector(0_ss, 0_ss, SimScalar(100.0f + i * 40.0f)), script);
            }
            addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
            constructorId = addUnit(sim, "ARMCK", ai, SimVector(-100_ss, 0_ss, 50_ss), script);
        }
    };
}
