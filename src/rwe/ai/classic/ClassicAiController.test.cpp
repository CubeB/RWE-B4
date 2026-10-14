#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/ai/AiBuildTree.h>
#include <rwe/ai/ai_test_util.h>
#include <rwe/ai/classic/ClassicAiController.h>
#include <rwe/ai/classic/ClassicAiPlacement.h>
#include <rwe/ai/classic/ClassicRandom.h>
#include <rwe/sim/FeatureDefinition.h>
#include <sstream>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        UnitDefinition classicDef(bool commander, bool builder, bool mobile, const std::string& weapon, const std::string& category)
        {
            auto d = makeDef(commander, builder, mobile, weapon, 100u);
            d.category = category;
            d.buildCostMetal = Metal(100.0f);
            d.buildCostEnergy = Energy(500.0f);
            return d;
        }

        /** A small world: two players, a flat map, and the handful of types the original's policy has to choose among. */
        struct World
        {
            GameSimulation sim;
            std::shared_ptr<CobScript> script;
            std::shared_ptr<AiBuildTree> tree;
            PlayerId ai{0};
            PlayerId enemy{0};

            World() : sim(makeFlatTerrain(128, 128), 0u, 0, 0), script(makeEmptyCobScript()), tree(std::make_shared<AiBuildTree>())
            {
                WeaponDefinition laser{};
                laser.maxRange = 200_ss;
                laser.reloadTime = 1_ss;
                laser.burst = 1;
                laser.damage["DEFAULT"] = 30u;
                sim.weaponDefinitions["LASER"] = laser;

                auto commander = classicDef(true, true, true, "LASER", "ARM LEVEL1 COMMANDER");
                commander.canCapture = true;
                commander.mobileStandOrders = true;
                commander.fireStandOrders = true;
                commander.energyMake = Energy(25.0f);
                commander.metalMake = Metal(2.0f);
                sim.unitDefinitions["ARMCOM"] = commander;

                auto mex = classicDef(false, false, false, "", "ARM LEVEL1 METAL");
                mex.extractsMetal = Metal(1.0f);
                mex.buildCostMetal = Metal(50.0f);
                mex.buildCostEnergy = Energy(500.0f);
                mex.energyUse = Energy(3.0f);
                sim.unitDefinitions["ARMMEX"] = mex;

                auto solar = classicDef(false, false, false, "", "ARM LEVEL1 ENERGY");
                solar.energyMake = Energy(20.0f);
                solar.buildCostMetal = Metal(155.0f);
                solar.buildCostEnergy = Energy(0.0f);
                sim.unitDefinitions["ARMSOLAR"] = solar;

                auto lab = classicDef(false, true, false, "", "ARM LEVEL1 PLANT");
                lab.buildCostMetal = Metal(100.0f);
                sim.unitDefinitions["ARMLAB"] = lab;

                auto maker = classicDef(false, false, false, "", "ARM LEVEL1 METAL");
                maker.makesMetal = Metal(1.0f);
                maker.onOffable = true;
                maker.energyUse = Energy(60.0f);
                sim.unitDefinitions["ARMMAKR"] = maker;

                auto tank = classicDef(false, false, true, "LASER", "ARM LEVEL1 TANK WEAPON");
                tank.mobileStandOrders = true;
                tank.fireStandOrders = true;
                sim.unitDefinitions["ARMPW"] = tank;

                auto fighter = classicDef(false, false, true, "LASER", "ARM LEVEL1 VTOL WEAPON");
                fighter.canFly = true;
                sim.unitDefinitions["ARMFIG"] = fighter;

                auto boat = classicDef(false, false, true, "LASER", "ARM LEVEL1 SHIP WEAPON");
                boat.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 5u, 255u};
                sim.unitDefinitions["ARMBOAT"] = boat;

                auto coreTank = classicDef(false, false, true, "LASER", "CORE LEVEL1 TANK WEAPON");
                sim.unitDefinitions["CORPW"] = coreTank;
                auto coreCom = classicDef(true, true, true, "LASER", "CORE LEVEL1 COMMANDER");
                sim.unitDefinitions["CORCOM"] = coreCom;

                tree->buildableBy["ARMCOM"] = {"ARMMEX", "ARMSOLAR", "ARMLAB", "ARMMAKR"};
                tree->buildableBy["ARMLAB"] = {"ARMPW", "ARMFIG"};

                ai = rwe::addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
                enemy = rwe::addPlayer(sim, "enemy", GamePlayerType::Human, "CORE");
            }

            UnitId add(const std::string& type, PlayerId owner, float x, float z)
            {
                return addUnit(sim, type, owner, SimVector(SimScalar(x), 0_ss, SimScalar(z)), script);
            }

            void addDeposits()
            {
                FeatureDefinition d{};
                d.name = "metalspot";
                d.footprintX = 2;
                d.footprintZ = 2;
                d.metal = 40;
                d.indestructible = true;
                d.blocking = false;
                d.damage = 1;
                auto id = sim.featureDefinitions.insert(d);
                for (auto [x, z] : {std::pair<int, int>{70, 64}, {76, 64}, {70, 72}, {80, 80}, {50, 50}, {60, 76}, {90, 60}, {40, 70}})
                {
                    sim.addFeature(id, x, z);
                }
            }

            ClassicAiConfig config(AiDifficulty difficulty = AiDifficulty::Hard, const std::string& profile = "") const
            {
                ClassicAiConfig c;
                c.difficulty = difficulty;
                c.profileText = profile;
                c.buildTree = tree;
                return c;
            }

            void advance(ClassicAiController& controller, int ticks, std::vector<PlayerCommand>& out)
            {
                for (int i = 0; i < ticks; ++i)
                {
                    sim.gameTime = GameTime(sim.gameTime.value + 1);
                    controller.tick(sim, out);
                }
            }
        };

        std::string describe(const PlayerCommand& c)
        {
            std::ostringstream s;
            auto unit = std::get_if<PlayerUnitCommand>(&c);
            if (!unit)
            {
                return "other";
            }
            s << unit->unit.value << ":";
            if (auto issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unit->command))
            {
                s << (issue->issueKind == PlayerUnitCommand::IssueOrder::Immediate ? "I " : "Q ");
                if (auto move = std::get_if<MoveOrder>(&issue->order))
                {
                    s << "move " << move->destination.x.value << "," << move->destination.z.value;
                }
                else if (auto patrol = std::get_if<PatrolOrder>(&issue->order))
                {
                    s << "patrol " << patrol->destination.x.value << "," << patrol->destination.z.value;
                }
                else if (auto attack = std::get_if<AttackOrder>(&issue->order))
                {
                    if (auto target = std::get_if<UnitId>(&attack->target))
                    {
                        s << "attack " << target->value;
                    }
                    else
                    {
                        s << "attack ground";
                    }
                }
                else if (auto build = std::get_if<BuildOrder>(&issue->order))
                {
                    s << "build " << build->unitType << " " << build->position.x.value << "," << build->position.z.value;
                }
                else
                {
                    s << "order";
                }
            }
            else if (auto queue = std::get_if<PlayerUnitCommand::ModifyBuildQueue>(&unit->command))
            {
                s << "queue " << queue->unitType << " " << queue->count;
            }
            else if (auto fire = std::get_if<PlayerUnitCommand::SetFireOrders>(&unit->command))
            {
                s << "fire " << static_cast<int>(fire->orders);
            }
            else if (auto move = std::get_if<PlayerUnitCommand::SetMovementOrders>(&unit->command))
            {
                s << "movement " << static_cast<int>(move->orders);
            }
            else if (auto onoff = std::get_if<PlayerUnitCommand::SetOnOff>(&unit->command))
            {
                s << "onoff " << onoff->on;
            }
            else if (std::holds_alternative<PlayerUnitCommand::Stop>(unit->command))
            {
                s << "stop";
            }
            else
            {
                s << "command";
            }
            return s.str();
        }

        std::vector<const BuildOrder*> buildOrders(const std::vector<PlayerCommand>& commands)
        {
            std::vector<const BuildOrder*> out;
            for (const auto& c : commands)
            {
                if (auto unit = std::get_if<PlayerUnitCommand>(&c))
                {
                    if (auto issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unit->command))
                    {
                        if (auto build = std::get_if<BuildOrder>(&issue->order))
                        {
                            out.push_back(build);
                        }
                    }
                }
            }
            return out;
        }

        template <typename Order>
        std::vector<std::pair<UnitId, Order>> immediateOrders(const std::vector<PlayerCommand>& commands)
        {
            std::vector<std::pair<UnitId, Order>> out;
            for (const auto& c : commands)
            {
                if (auto unit = std::get_if<PlayerUnitCommand>(&c))
                {
                    if (auto issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unit->command))
                    {
                        if (auto order = std::get_if<Order>(&issue->order))
                        {
                            out.emplace_back(unit->unit, *order);
                        }
                    }
                }
            }
            return out;
        }

        /**
         * Plays a controller against its world with a builder that is as fast
         * as it can be: a build order puts the finished building down at once,
         * and a queued unit appears beside its factory. That is not how the
         * game builds, and it is not meant to be; it is what lets a few
         * thousand ticks of the controller's own decisions play out without
         * the simulation's construction in the way.
         *
         * The economy is stood in for the same way -- income follows what has
         * been built -- because the controller reads it and nothing here
         * spends or earns.
         */
        void driveWorld(World& w, ClassicAiController& controller, int ticks, std::vector<std::string>* log, std::map<std::string, int>* built)
        {
            int extractors = 0;
            int solars = 0;
            for (int t = 0; t < ticks; ++t)
            {
                w.sim.gameTime = GameTime(w.sim.gameTime.value + 1);
                auto& player = w.sim.getPlayer(w.ai);
                player.metal = Metal(1000.0f);
                player.energy = Energy(1000.0f);
                player.previousMetalProductionBuffer = Metal(2.0f + 3.0f * static_cast<float>(extractors));
                player.previousEnergyProductionBuffer = Energy(25.0f + 20.0f * static_cast<float>(solars));

                std::vector<PlayerCommand> out;
                controller.tick(w.sim, out);

                for (const auto& c : out)
                {
                    if (log)
                    {
                        log->push_back(std::to_string(w.sim.gameTime.value) + " " + describe(c));
                    }
                    auto unit = std::get_if<PlayerUnitCommand>(&c);
                    if (!unit)
                    {
                        continue;
                    }
                    if (auto issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unit->command))
                    {
                        if (auto build = std::get_if<BuildOrder>(&issue->order))
                        {
                            const auto& def = w.sim.unitDefinitions.at(build->unitType);
                            auto rect = w.sim.computeFootprintRegion(build->position, def.movementCollisionInfo);
                            auto mc = w.sim.getAdHocMovementClass(def.movementCollisionInfo);
                            // Two builders can be sent to the same place, as the original's can.
                            if (w.sim.canBeBuiltAt(mc, def.yardMap, def.yardMapContainsGeo, static_cast<unsigned int>(rect.x), static_cast<unsigned int>(rect.y)))
                            {
                                addUnit(w.sim, build->unitType, w.ai, build->position, w.script);
                                if (built)
                                {
                                    ++(*built)[build->unitType];
                                }
                                if (build->unitType == "ARMMEX")
                                {
                                    ++extractors;
                                }
                                if (build->unitType == "ARMSOLAR")
                                {
                                    ++solars;
                                }
                            }
                        }
                    }
                    else if (auto queue = std::get_if<PlayerUnitCommand::ModifyBuildQueue>(&unit->command))
                    {
                        if (queue->count > 0 && w.sim.getUnitState(unit->unit).isAlive())
                        {
                            auto at = w.sim.getUnitState(unit->unit).position;
                            auto count = 0;
                            for (const auto& [id, u] : w.sim.units)
                            {
                                (void)id;
                                if (u.owner == w.ai)
                                {
                                    ++count;
                                }
                            }
                            if (count < 150)
                            {
                                addUnit(w.sim, queue->unitType, w.ai, SimVector(at.x + SimScalar(48.0f + static_cast<float>(count % 7) * 4.0f), 0_ss, at.z + SimScalar(static_cast<float>(count % 11) * 6.0f)), w.script);
                                if (built)
                                {
                                    ++(*built)[queue->unitType];
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    TEST_CASE("the classic controller reports the original's income handicap", "[ai][classic]")
    {
        World w;
        CHECK(ClassicAiController(w.ai, w.config(AiDifficulty::Easy), 1).getProfile().resourceCheatMultiplier == 0.5_ssf);
        CHECK(ClassicAiController(w.ai, w.config(AiDifficulty::Standard), 1).getProfile().resourceCheatMultiplier == 0.7_ssf);
        CHECK(ClassicAiController(w.ai, w.config(AiDifficulty::Hard), 1).getProfile().resourceCheatMultiplier == 1_ss);
        CHECK(ClassicAiController(w.ai, w.config(AiDifficulty::Brutal), 1).getProfile().resourceCheatMultiplier == 1_ss);
        CHECK(ClassicAiController(w.ai, w.config(), 1).engine() == AiEngine::Classic);
    }

    TEST_CASE("an idle classic controller does nothing at all", "[ai][classic]")
    {
        World w;
        w.add("ARMCOM", w.ai, 0.0f, 0.0f);
        w.add("ARMLAB", w.ai, 100.0f, 0.0f);

        auto idleConfig = w.config();
        idleConfig.idle = true;
        ClassicAiController replaying(w.ai, idleConfig, 5);
        ClassicAiController idleDifficulty(w.ai, w.config(AiDifficulty::Idle), 5);

        std::vector<PlayerCommand> out;
        w.advance(replaying, 400, out);
        w.advance(idleDifficulty, 400, out);
        CHECK(out.empty());
    }

    TEST_CASE("the classic controller's commander builds an extractor first, on a deposit", "[ai][classic]")
    {
        World w;
        w.addDeposits();
        w.add("ARMCOM", w.ai, 0.0f, 0.0f);
        w.add("CORCOM", w.enemy, 900.0f, 900.0f);

        ClassicAiController controller(w.ai, w.config(), 99);
        std::vector<PlayerCommand> out;
        w.advance(controller, 120, out);

        auto builds = buildOrders(out);
        REQUIRE(!builds.empty());
        // No income at all: metal is the shortage, so the metal rating decides, and
        // only an extractor has one.
        CHECK(builds.front()->unitType == "ARMMEX");

        // On a deposit, not just somewhere: within one cell of a metal feature.
        bool onDeposit = false;
        for (const auto& [id, feature] : w.sim.features)
        {
            (void)id;
            if (std::abs(feature.position.x.value - builds.front()->position.x.value) < 8.0f
                && std::abs(feature.position.z.value - builds.front()->position.z.value) < 8.0f)
            {
                onDeposit = true;
            }
        }
        CHECK(onDeposit);
    }

    TEST_CASE("a complete classic game from a lone commander: extractors, power, a lab and an army", "[ai][classic]")
    {
        World w;
        w.addDeposits();
        w.add("ARMCOM", w.ai, 0.0f, 0.0f);
        w.add("CORCOM", w.enemy, 700.0f, 700.0f);
        w.add("CORPW", w.enemy, 650.0f, 650.0f);

        ClassicAiController controller(w.ai, w.config(), 2024);
        std::map<std::string, int> built;
        std::vector<std::string> log;
        driveWorld(w, controller, 9000, &log, &built);

        INFO("built: mex " << built["ARMMEX"] << ", solar " << built["ARMSOLAR"] << ", lab " << built["ARMLAB"] << ", tanks " << built["ARMPW"] << ", fighters " << built["ARMFIG"]);
        CHECK(built["ARMMEX"] >= 1);
        CHECK(built["ARMSOLAR"] >= 1);
        CHECK(built["ARMLAB"] >= 1);
        CHECK(built["ARMPW"] + built["ARMFIG"] >= 6);

        // And once there are six, the land force goes for the enemy.
        bool attacked = false;
        for (const auto& line : log)
        {
            if (line.find("attack") != std::string::npos)
            {
                attacked = true;
            }
        }
        CHECK(attacked);

        // Never a transport order, never a guard, never a capture, never an
        // aircraft sent to attack: nothing in the log is anything but the five
        // kinds the register describes.
        for (const auto& line : log)
        {
            CHECK(line.find("order") == std::string::npos);
        }
    }

    TEST_CASE("two classic controllers from one seed emit byte-identical commands", "[ai][classic][determinism]")
    {
        std::vector<std::string> a;
        std::vector<std::string> b;
        for (auto* log : {&a, &b})
        {
            World w;
            w.addDeposits();
            w.add("ARMCOM", w.ai, 0.0f, 0.0f);
            w.add("CORCOM", w.enemy, 700.0f, 700.0f);
            ClassicAiController controller(w.ai, w.config(), 31337);
            driveWorld(w, controller, 3000, log, nullptr);
        }
        REQUIRE(!a.empty());
        CHECK(a == b);

        // And a different seed does something else.
        std::vector<std::string> c;
        {
            World w;
            w.addDeposits();
            w.add("ARMCOM", w.ai, 0.0f, 0.0f);
            w.add("CORCOM", w.enemy, 700.0f, 700.0f);
            ClassicAiController controller(w.ai, w.config(), 31338);
            driveWorld(w, controller, 3000, &c, nullptr);
        }
        CHECK(a != c);
    }

    TEST_CASE("a fresh controller on a game in progress starts blank and does the same thing twice", "[ai][classic][determinism]")
    {
        // A saved game rebuilds its controllers blank, as the original does.
        // Two blank controllers handed the same position agree.
        auto run = []() {
            World w;
            w.addDeposits();
            w.add("ARMCOM", w.ai, 0.0f, 0.0f);
            w.add("ARMLAB", w.ai, 120.0f, 40.0f);
            for (int i = 0; i < 8; ++i)
            {
                w.add("ARMPW", w.ai, 60.0f + 10.0f * static_cast<float>(i), 80.0f);
            }
            w.add("CORCOM", w.enemy, 700.0f, 700.0f);
            w.sim.gameTime = GameTime(4000);
            ClassicAiController controller(w.ai, w.config(), 7);
            std::vector<PlayerCommand> out;
            w.advance(controller, 400, out);
            std::vector<std::string> lines;
            for (const auto& c : out)
            {
                lines.push_back(describe(c));
            }
            return lines;
        };
        auto first = run();
        auto second = run();
        CHECK(!first.empty());
        CHECK(first == second);
    }

    TEST_CASE("the profile text decides what is built", "[ai][classic]")
    {
        const std::string text = "plan easy\nlimit armmex 0\nplan hard\nlimit armsolar 0\n";

        auto types = [&](AiDifficulty difficulty) {
            World w;
            w.addDeposits();
            w.add("ARMCOM", w.ai, 0.0f, 0.0f);
            ClassicAiController controller(w.ai, w.config(difficulty, text), 12);
            std::map<std::string, int> built;
            driveWorld(w, controller, 3000, nullptr, &built);
            return built;
        };

        auto easy = types(AiDifficulty::Easy);
        CHECK(easy["ARMMEX"] == 0);

        auto hard = types(AiDifficulty::Hard);
        CHECK(hard["ARMMEX"] >= 1);
        CHECK(hard["ARMSOLAR"] == 0);
    }

    TEST_CASE("a limit counts completed units only", "[ai][classic]")
    {
        // limit 2 on extractors, but nothing completes in this world: the AI
        // keeps ordering them. The original's "overbuild" bug.
        World w;
        w.addDeposits();
        w.add("ARMCOM", w.ai, 0.0f, 0.0f);
        ClassicAiController controller(w.ai, w.config(AiDifficulty::Hard, "limit armmex 2\n"), 4);
        std::vector<PlayerCommand> out;
        w.advance(controller, 1200, out);
        int mexOrders = 0;
        for (const auto* b : buildOrders(out))
        {
            if (b->unitType == "ARMMEX")
            {
                ++mexOrders;
            }
        }
        // Nothing was ever finished, so the count never reached the limit.
        CHECK(mexOrders > 2);
    }

    TEST_CASE("standing policies are forced on the 30-tick sweep, and only where they differ", "[ai][classic]")
    {
        World w;
        auto commander = w.add("ARMCOM", w.ai, 0.0f, 0.0f);
        auto tank = w.add("ARMPW", w.ai, 50.0f, 0.0f);
        auto settled = w.add("ARMPW", w.ai, 80.0f, 0.0f);
        w.sim.getUnitState(commander).fireOrders = UnitFireOrders::HoldFire;
        w.sim.getUnitState(commander).moveOrders = UnitMovementOrders::Roam;
        w.sim.getUnitState(tank).fireOrders = UnitFireOrders::ReturnFire;
        w.sim.getUnitState(tank).moveOrders = UnitMovementOrders::HoldPosition;
        // `settled` already stands as the original would put it.
        w.sim.getUnitState(settled).fireOrders = UnitFireOrders::FireAtWill;
        w.sim.getUnitState(settled).moveOrders = UnitMovementOrders::Roam;

        ClassicAiController controller(w.ai, w.config(), 3);
        std::vector<PlayerCommand> out;
        w.advance(controller, 29, out);
        for (const auto& c : out)
        {
            CHECK(describe(c).find("fire") == std::string::npos);
            CHECK(describe(c).find("movement") == std::string::npos);
        }
        out.clear();
        w.advance(controller, 1, out);

        std::vector<std::string> lines;
        for (const auto& c : out)
        {
            lines.push_back(describe(c));
        }
        auto has = [&](const std::string& s) { return std::find(lines.begin(), lines.end(), s) != lines.end(); };
        // A cancapture unit is put on maneuver (1) and every other eligible type on roam (2).
        CHECK(has(std::to_string(commander.value) + ":fire 2"));
        CHECK(has(std::to_string(commander.value) + ":movement 1"));
        CHECK(has(std::to_string(tank.value) + ":fire 2"));
        CHECK(has(std::to_string(tank.value) + ":movement 2"));
        for (const auto& line : lines)
        {
            CHECK(line.rfind(std::to_string(settled.value) + ":", 0) != 0);
        }
    }

    TEST_CASE("a metal converter goes off when energy is no more than twice metal, and on when it is rich", "[ai][classic]")
    {
        World w;
        auto maker = w.add("ARMMAKR", w.ai, 0.0f, 0.0f);
        auto& player = w.sim.getPlayer(w.ai);

        ClassicAiController controller(w.ai, w.config(), 8);

        SECTION("poor in energy: switched off")
        {
            player.energy = Energy(300.0f);
            player.metal = Metal(200.0f);
            w.sim.getUnitState(maker).activated = true;
            std::vector<PlayerCommand> out;
            w.advance(controller, 1, out);
            std::vector<std::string> lines;
            for (const auto& c : out)
            {
                lines.push_back(describe(c));
            }
            CHECK(std::find(lines.begin(), lines.end(), std::to_string(maker.value) + ":onoff 0") != lines.end());
        }

        SECTION("rich in energy with positive net energy: switched on, never off")
        {
            player.energy = Energy(900.0f);
            player.metal = Metal(100.0f);
            player.previousEnergyProductionBuffer = Energy(60.0f);
            std::vector<PlayerCommand> out;
            // Slot 1 runs every 30 ticks; rand(5) is nonzero four times in five,
            // so a handful of passes always turns it on.
            w.advance(controller, 300, out);
            bool on = false;
            bool off = false;
            for (const auto& c : out)
            {
                auto d = describe(c);
                on = on || d == std::to_string(maker.value) + ":onoff 1";
                off = off || d == std::to_string(maker.value) + ":onoff 0";
            }
            CHECK(on);
            CHECK(!off);
        }

        SECTION("rich in energy but net energy not positive: left alone")
        {
            player.energy = Energy(900.0f);
            player.metal = Metal(100.0f);
            player.previousEnergyProductionBuffer = Energy(0.0f);
            w.sim.getUnitState(maker).activated = true;
            std::vector<PlayerCommand> out;
            w.advance(controller, 300, out);
            for (const auto& c : out)
            {
                CHECK(describe(c).find("onoff") == std::string::npos);
            }
        }
    }

    namespace
    {
        // Land units, each some way from the others and from a lab at the origin.
        std::vector<UnitId> addSquad(World& w, int count, const std::string& type = "ARMPW")
        {
            std::vector<UnitId> ids;
            for (int i = 0; i < count; ++i)
            {
                ids.push_back(w.add(type, w.ai, 100.0f + 12.0f * static_cast<float>(i), 100.0f + 6.0f * static_cast<float>(i % 3)));
            }
            return ids;
        }
    }

    TEST_CASE("an assault of six goes for the enemy unit nearest its own centre", "[ai][classic]")
    {
        World w;
        auto lab = w.add("ARMLAB", w.ai, -200.0f, -200.0f);
        (void)lab;
        auto squad = addSquad(w, 6);
        auto far = w.add("CORPW", w.enemy, -300.0f, 900.0f);
        auto near = w.add("CORPW", w.enemy, 450.0f, 150.0f);
        auto flying = w.add("CORPW", w.enemy, 120.0f, 120.0f); // would be nearest, but it is airborne
        w.sim.flyingUnitsSet.insert(flying);
        (void)far;

        ClassicAiController controller(w.ai, w.config(), 6);
        std::vector<PlayerCommand> out;
        w.advance(controller, 300, out);
        CHECK(immediateOrders<AttackOrder>(out).empty());

        out.clear();
        w.advance(controller, 2, out);
        auto attacks = immediateOrders<AttackOrder>(out);
        REQUIRE(attacks.size() == 6);
        for (const auto& [unit, order] : attacks)
        {
            (void)unit;
            REQUIRE(std::holds_alternative<UnitId>(order.target));
            CHECK(std::get<UnitId>(order.target) == near);
        }
        CHECK(immediateOrders<MoveOrder>(out).empty());
    }

    TEST_CASE("an assault under six rallies at the first of defences, buildings and builders", "[ai][classic]")
    {
        World w;
        // Buildings (group 1) and a mobile builder (group 4): buildings first.
        w.add("ARMLAB", w.ai, -200.0f, -200.0f);
        w.add("ARMCOM", w.ai, 600.0f, 600.0f);
        addSquad(w, 5);
        w.add("CORPW", w.enemy, 450.0f, 150.0f);

        ClassicAiController controller(w.ai, w.config(), 6);
        std::vector<PlayerCommand> out;
        w.advance(controller, 300, out);
        out.clear();
        w.advance(controller, 2, out);

        auto moves = immediateOrders<MoveOrder>(out);
        REQUIRE(moves.size() == 5);
        for (const auto& [unit, order] : moves)
        {
            (void)unit;
            CHECK(std::abs(order.destination.x.value - (-200.0f)) < 1.0f);
            CHECK(std::abs(order.destination.z.value - (-200.0f)) < 1.0f);
        }
        CHECK(immediateOrders<AttackOrder>(out).empty());
    }

    TEST_CASE("an assault with no rally point falls through to attack whatever its size", "[ai][classic]")
    {
        World w;
        addSquad(w, 4);
        auto enemy = w.add("CORPW", w.enemy, 450.0f, 150.0f);

        ClassicAiController controller(w.ai, w.config(), 6);
        std::vector<PlayerCommand> out;
        w.advance(controller, 300, out);
        out.clear();
        w.advance(controller, 2, out);

        auto attacks = immediateOrders<AttackOrder>(out);
        REQUIRE(attacks.size() == 4);
        CHECK(std::get<UnitId>(attacks.front().second.target) == enemy);
    }

    TEST_CASE("once attacking, four or five continue and three fall back", "[ai][classic]")
    {
        World w;
        w.add("ARMLAB", w.ai, -200.0f, -200.0f);
        auto squad = addSquad(w, 6);
        w.add("CORPW", w.enemy, 450.0f, 150.0f);

        ClassicAiController controller(w.ai, w.config(), 6);
        std::vector<PlayerCommand> out;
        w.advance(controller, 302, out);
        REQUIRE(immediateOrders<AttackOrder>(out).size() == 6);

        // Two die: four left, and it has begun, so it carries on.
        w.sim.getUnitState(squad[0]).markAsDead();
        w.sim.getUnitState(squad[1]).markAsDead();
        out.clear();
        w.advance(controller, 300, out);
        CHECK(immediateOrders<AttackOrder>(out).size() == 4);
        CHECK(immediateOrders<MoveOrder>(out).empty());

        // A third: three or fewer, and it falls back to the rally point.
        w.sim.getUnitState(squad[2]).markAsDead();
        out.clear();
        w.advance(controller, 300, out);
        CHECK(immediateOrders<AttackOrder>(out).empty());
        CHECK(immediateOrders<MoveOrder>(out).size() == 3);

        // And having fallen back, it needs six again before it goes: add three
        // and it has six, which is enough to begin.
        for (int i = 0; i < 3; ++i)
        {
            w.add("ARMPW", w.ai, 130.0f + 5.0f * static_cast<float>(i), 130.0f);
        }
        out.clear();
        w.advance(controller, 600, out);
        CHECK(immediateOrders<AttackOrder>(out).size() >= 6);
    }

    TEST_CASE("water units make their own assault, and aircraft are never sent at anything", "[ai][classic]")
    {
        World w;
        w.add("ARMLAB", w.ai, -200.0f, -200.0f);
        auto boats = addSquad(w, 6, "ARMBOAT");
        auto planes = addSquad(w, 8, "ARMFIG");
        for (auto id : planes)
        {
            UnitPhysicsInfoAir air;
            air.movementState = AirMovementStateFlying();
            w.sim.getUnitState(id).physics = air;
            w.sim.flyingUnitsSet.insert(id);
        }
        auto enemy = w.add("CORPW", w.enemy, 450.0f, 150.0f);

        ClassicAiController controller(w.ai, w.config(), 6);
        std::vector<PlayerCommand> out;
        w.advance(controller, 1500, out);

        auto attacks = immediateOrders<AttackOrder>(out);
        REQUIRE(!attacks.empty());
        for (const auto& [unit, order] : attacks)
        {
            // Only boats attack, and always the enemy.
            CHECK(std::find(boats.begin(), boats.end(), unit) != boats.end());
            CHECK(std::get<UnitId>(order.target) == enemy);
        }
        for (auto id : planes)
        {
            for (const auto& [unit, order] : attacks)
            {
                (void)order;
                CHECK(!(unit == id));
            }
        }

        // With eight aircraft the route is a patrol to a map edge, and with
        // fewer than five a move and patrols near the base; either way a
        // patrol or a move and never an attack.
        bool routed = false;
        for (const auto& [unit, order] : immediateOrders<PatrolOrder>(out))
        {
            (void)order;
            routed = routed || std::find(planes.begin(), planes.end(), unit) != planes.end();
        }
        CHECK(routed);
    }

    TEST_CASE("units held by a mission and units still being built are not the controller's to use", "[ai][classic]")
    {
        World w;
        w.add("ARMLAB", w.ai, -200.0f, -200.0f);
        auto squad = addSquad(w, 6);
        // One of the six is the mission's, one is a frame.
        w.sim.getUnitState(squad[0]).heldByMission = true;
        w.sim.getUnitState(squad[1]).buildTimeCompleted = 0;
        w.add("CORPW", w.enemy, 450.0f, 150.0f);

        ClassicAiController controller(w.ai, w.config(), 6);
        std::vector<PlayerCommand> out;
        w.advance(controller, 302, out);

        // Four usable units: it rallies rather than attacks.
        CHECK(immediateOrders<AttackOrder>(out).empty());
        auto moves = immediateOrders<MoveOrder>(out);
        CHECK(moves.size() == 4);
        for (const auto& [unit, order] : moves)
        {
            (void)order;
            CHECK(!(unit == squad[0]));
            CHECK(!(unit == squad[1]));
        }
    }

    TEST_CASE("a cancapture builder that is hit holds its work back for a while", "[ai][classic]")
    {
        auto firstBuildTick = [](bool hit) {
            World w;
            w.addDeposits();
            auto commander = w.add("ARMCOM", w.ai, 0.0f, 0.0f);
            ClassicAiController controller(w.ai, w.config(), 21);
            std::vector<PlayerCommand> out;
            w.advance(controller, 79, out);
            if (hit)
            {
                w.sim.getUnitState(commander).hitPoints = 50;
            }
            for (int i = 0; i < 700; ++i)
            {
                out.clear();
                w.advance(controller, 1, out);
                if (!buildOrders(out).empty())
                {
                    return static_cast<int>(w.sim.gameTime.value);
                }
            }
            return -1;
        };

        auto unhurt = firstBuildTick(false);
        auto hurt = firstBuildTick(true);
        REQUIRE(unhurt > 0);
        REQUIRE(hurt > 0);
        // Slot 4 first works at tick 91; a hit at tick 80 holds it off for 30 to 329 more.
        CHECK(unhurt == 91);
        CHECK(hurt > unhurt);
    }

    TEST_CASE("a half-built factory is not asked to build", "[ai][classic]")
    {
        World w;
        auto lab = w.add("ARMLAB", w.ai, 100.0f, 0.0f);
        w.sim.getUnitState(lab).buildTimeCompleted = 10;
        ClassicAiController controller(w.ai, w.config(), 2);
        std::vector<PlayerCommand> out;
        w.advance(controller, 200, out);
        for (const auto& c : out)
        {
            CHECK(describe(c).find("queue") == std::string::npos);
        }
    }

    // -------------------------------------------------------------- placement

    TEST_CASE("an extractor is put on the nearest deposit", "[ai][classic][placement]")
    {
        World w;
        w.addDeposits();
        ClassicPlacement placement;
        auto rng = ClassicRandom::fromSeed(5);
        placement.initialise(w.sim, rng);
        REQUIRE(placement.resourceCells().size() == 8);

        // Standing beside the deposit at (70,64): heightmap cell 70 is world x 96.
        auto at = SimVector(100_ss, 0_ss, 16_ss);
        auto site = placement.findSite(w.sim, w.sim.unitDefinitions.at("ARMMEX"), at, at, rng);
        REQUIRE(site);
        bool onDeposit = false;
        for (const auto& [id, feature] : w.sim.features)
        {
            (void)id;
            onDeposit = onDeposit || (feature.position.x == site->x && feature.position.z == site->z);
        }
        CHECK(onDeposit);
        // Nearest first: of the eight, the one at cell (70, 64) is the closest to here.
        CHECK(site->x.value == 112.0f);
        CHECK(site->z.value == 16.0f);
        CHECK(placement.sharedRadius() == 0);
    }

    TEST_CASE("the resource cache is built once and never rebuilt", "[ai][classic][placement]")
    {
        World w;
        w.addDeposits();
        ClassicPlacement placement;
        auto rng = ClassicRandom::fromSeed(5);
        placement.initialise(w.sim, rng);
        REQUIRE(placement.resourceCells().size() == 8);

        FeatureDefinition d{};
        d.name = "latespot";
        d.footprintX = 2;
        d.footprintZ = 2;
        d.metal = 40;
        d.indestructible = true;
        d.damage = 1;
        w.sim.addFeature(w.sim.featureDefinitions.insert(d), 30, 30);

        placement.initialise(w.sim, rng);
        CHECK(placement.resourceCells().size() == 8);
    }

    TEST_CASE("the lattices are drawn once, in range, and offset about the midpoint", "[ai][classic][placement]")
    {
        for (std::uint64_t seed = 1; seed <= 40; ++seed)
        {
            World w;
            ClassicPlacement placement;
            auto rng = ClassicRandom::fromSeed(seed);
            placement.initialise(w.sim, rng);
            const auto& a = placement.firstLattice();
            const auto& b = placement.secondLattice();
            CHECK(a.margin == 3);
            CHECK(a.spacingX >= 11);
            CHECK(a.spacingX <= 20);
            CHECK(a.spacingZ >= 11);
            CHECK(a.spacingZ <= 13);
            CHECK(a.offsetX == a.spacingX / 2);
            CHECK(b.margin == 6);
            CHECK(b.spacingX >= 14);
            CHECK(b.spacingX <= 33);
            CHECK(b.spacingZ >= 14);
            CHECK(b.spacingZ <= 16);
            CHECK(b.offsetZ == b.spacingZ / 2);
        }
    }

    TEST_CASE("the shared radius grows by 160 a search and is only reset by a success", "[ai][classic][placement]")
    {
        World w;
        // A footprint that fits nowhere on this map: every search fails.
        auto huge = classicDef(false, false, false, "", "ARM LEVEL1");
        huge.movementCollisionInfo = UnitDefinition::AdHocMovementClass{200u, 200u, 255u, 255u, 0u, 255u};
        w.sim.unitDefinitions["ARMHUGE"] = huge;

        ClassicPlacement placement;
        auto rng = ClassicRandom::fromSeed(5);
        placement.initialise(w.sim, rng);

        auto at = SimVector(0_ss, 0_ss, 0_ss);
        const auto& def = w.sim.unitDefinitions.at("ARMHUGE");
        CHECK(!placement.findSite(w.sim, def, at, at, rng));
        CHECK(placement.sharedRadius() == 160);
        CHECK(!placement.findSite(w.sim, def, at, at, rng));
        CHECK(placement.sharedRadius() == 320);

        // A building that fits succeeds, and the radius drops to zero.
        auto site = placement.findSite(w.sim, w.sim.unitDefinitions.at("ARMSOLAR"), at, at, rng);
        REQUIRE(site);
        CHECK(placement.sharedRadius() == 0);
    }

    TEST_CASE("ordinary buildings are placed on ground a unit of that type could be built on", "[ai][classic][placement]")
    {
        for (std::uint64_t seed = 1; seed <= 25; ++seed)
        {
            World w;
            w.add("ARMLAB", w.ai, 40.0f, 40.0f);
            ClassicPlacement placement;
            auto rng = ClassicRandom::fromSeed(seed);
            placement.initialise(w.sim, rng);

            const auto& def = w.sim.unitDefinitions.at("ARMSOLAR");
            auto at = SimVector(0_ss, 0_ss, 0_ss);
            auto site = placement.findSite(w.sim, def, at, at, rng);
            REQUIRE(site);
            auto rect = w.sim.computeFootprintRegion(*site, def.movementCollisionInfo);
            auto mc = w.sim.getAdHocMovementClass(def.movementCollisionInfo);
            CHECK(w.sim.canBeBuiltAt(mc, def.yardMap, def.yardMapContainsGeo, static_cast<unsigned int>(rect.x), static_cast<unsigned int>(rect.y)));
        }
    }
}
