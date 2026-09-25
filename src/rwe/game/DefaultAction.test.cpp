#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/game/DefaultAction.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        void registerModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /**
         * The definitions here carry only the flags S:103's two ladders name,
         * under the names of the shipped units whose behaviour they stand for.
         * They are not parsed out of the FBIs -- nothing in the test harness
         * reads them -- so what a Peewee is here is "canmove, canguard, can
         * attack, not a builder", which is the part of ARMPW.FBI the ladder
         * ever looks at.
         */
        UnitDefinition makeMobileDef(unsigned int footprint = 2u)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canGuard = true;
            d.maxHitPoints = 200;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{footprint, footprint, 255u, 255u, 0u, 0u};
            return d;
        }

        /** ARMPW: it walks, it shoots, it escorts, and it builds nothing. */
        UnitDefinition makePeeweeDef()
        {
            auto d = makeMobileDef();
            d.canAttack = true;
            return d;
        }

        /** ARMTSHIP, the Hulk: a crane transport, so canload without canfly. */
        UnitDefinition makeHulkDef()
        {
            auto d = makeMobileDef();
            d.canLoad = true;
            d.transportCapacity = 20;
            d.transportSize = 3;
            return d;
        }

        /** ARMATLAS: the same, plus canfly, which is the whole cursor split. */
        UnitDefinition makeAtlasDef()
        {
            auto d = makeHulkDef();
            d.canFly = true;
            d.transportCapacity = 1;
            return d;
        }

        /** ARMCK: a builder, and so the only thing here that can repair. */
        UnitDefinition makeConstructionKbotDef()
        {
            auto d = makeMobileDef();
            d.builder = true;
            d.canReclamate = true;
            return d;
        }

        /** ARMCOM: cancapture is what puts it above the attack arm. */
        UnitDefinition makeCommanderDef()
        {
            auto d = makeMobileDef();
            d.canAttack = true;
            d.canCapture = true;
            d.canReclamate = true;
            d.builder = true;
            return d;
        }

        /** ARMFIG. */
        UnitDefinition makeFighterDef()
        {
            auto d = makeMobileDef();
            d.canAttack = true;
            d.canFly = true;
            return d;
        }

        /** ARMLAB: canmove, so it has a MOVE button, but bmcode=0 and no mover. */
        UnitDefinition makeFactoryDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.builder = true;
            d.canMove = true;
            d.maxHitPoints = 200;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{6u, 6u, 255u, 255u, 0u, 0u};
            return d;
        }

        /** ARMASP, a repair pad: builder and isairbase, and switched on. */
        UnitDefinition makeRepairPadDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.builder = true;
            d.isAirBase = true;
            d.maxHitPoints = 200;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 0u, 0u};
            return d;
        }

        template <typename T>
        const T* orderOf(const DefaultAction& action)
        {
            auto o = std::get_if<DefaultActionOrder>(&action.action);
            if (o == nullptr)
            {
                return nullptr;
            }
            return std::get_if<T>(&o->order);
        }

        bool isSelect(const DefaultAction& action)
        {
            return std::holds_alternative<DefaultActionSelect>(action.action);
        }

        bool isMove(const DefaultAction& action)
        {
            return std::holds_alternative<DefaultActionMove>(action.action);
        }

        struct Fixture
        {
            GameSimulation sim;
            PlayerId player;
            PlayerId enemy;
            std::shared_ptr<CobScript> script;

            Fixture() : sim(makeFlatTerrain(64, 64), 0u, 0, 0),
                        player(addPlayer(sim, "me")),
                        enemy(addPlayer(sim, "them")),
                        script(makeEmptyCobScript({"base"}))
            {
                registerModel(sim);
                sim.unitDefinitions["peewee"] = makePeeweeDef();
                sim.unitDefinitions["hulk"] = makeHulkDef();
                sim.unitDefinitions["atlas"] = makeAtlasDef();
                sim.unitDefinitions["conkbot"] = makeConstructionKbotDef();
                sim.unitDefinitions["commander"] = makeCommanderDef();
                sim.unitDefinitions["fighter"] = makeFighterDef();
                sim.unitDefinitions["pad"] = makeRepairPadDef();
                sim.unitDefinitions["factory"] = makeFactoryDef();
            }

            UnitId spawn(const std::string& unitType, PlayerId owner, SimScalar x)
            {
                return addUnitOfType(sim, unitType, owner, SimVector(x, 0_ss, 0_ss), script);
            }

            DefaultAction act(DefaultActionScheme scheme, UnitId orderer, UnitId hovered)
            {
                return computeDefaultAction(sim, scheme, orderer, hovered, std::nullopt);
            }
        };
    }

    TEST_CASE("the default action ladder", "[defaultaction]")
    {
        Fixture f;

        SECTION("a Peewee over a friendly Hulk escorts it in right-click mode and selects it in left-click mode")
        {
            // The pair the roadmap's "units ordering themselves aboard" item
            // asked about, from the passenger's side. There is no boarding
            // order in the original: the Peewee gets FOLLOW_GROUND, ground
            // mission row 27, display "Guarding".
            auto peewee = f.spawn("peewee", f.player, 0_ss);
            auto hulk = f.spawn("hulk", f.player, 100_ss);

            auto right = f.act(DefaultActionScheme::RightClickDefault, peewee, hulk);
            REQUIRE(orderOf<GuardOrder>(right) != nullptr);
            REQUIRE(orderOf<GuardOrder>(right)->target == hulk);

            auto left = f.act(DefaultActionScheme::LeftClickDefault, peewee, hulk);
            REQUIRE(isSelect(left));
            REQUIRE(left.cursor == CursorType::Select);
        }

        SECTION("an Atlas over a friendly Peewee loads it in right-click mode and selects it in left-click mode")
        {
            // The same click from the transport's side, which is the only
            // side the original has: every one of the five CanLoadUnit call
            // sites passes the ordering unit as the transport.
            auto atlas = f.spawn("atlas", f.player, 0_ss);
            auto peewee = f.spawn("peewee", f.player, 100_ss);

            auto right = f.act(DefaultActionScheme::RightClickDefault, atlas, peewee);
            REQUIRE(orderOf<LoadOrder>(right) != nullptr);
            REQUIRE(orderOf<LoadOrder>(right)->target == peewee);

            // In this scheme the cursor is feedback and says nothing about
            // the order: 0x43EB02 answers cursorselect over one of your own,
            // and the right button issues without consulting it.
            REQUIRE(right.cursor == CursorType::Select);

            auto left = f.act(DefaultActionScheme::LeftClickDefault, atlas, peewee);
            REQUIRE(isSelect(left));
        }

        SECTION("the load cursor splits on canfly, and on nothing else")
        {
            auto atlas = f.spawn("atlas", f.player, 0_ss);
            auto hulk = f.spawn("hulk", f.player, 100_ss);
            auto peewee = f.spawn("peewee", f.player, 200_ss);

            auto byAir = f.act(DefaultActionScheme::MoveButton, atlas, peewee);
            REQUIRE(orderOf<LoadOrder>(byAir) != nullptr);
            REQUIRE(byAir.cursor == CursorType::Pickup);

            auto byCrane = f.act(DefaultActionScheme::MoveButton, hulk, peewee);
            REQUIRE(orderOf<LoadOrder>(byCrane) != nullptr);
            REQUIRE(byCrane.cursor == CursorType::Load);
        }

        SECTION("a Peewee over a damaged friendly does not repair it")
        {
            // S:103's sketch calls this one a move. It is a guard: the ladder
            // reaches its guard arm before its move arm, and every mobile
            // unit in the shipped data says canguard. What the case is really
            // pinning is that a Peewee is not a builder, so the repair arm
            // above cannot fire; with canguard cleared it does fall through
            // to the move.
            auto peewee = f.spawn("peewee", f.player, 0_ss);
            auto hurt = f.spawn("peewee", f.player, 100_ss);
            f.sim.getUnitState(hurt).hitPoints = 50;

            auto action = f.act(DefaultActionScheme::RightClickDefault, peewee, hurt);
            REQUIRE(orderOf<RepairOrder>(action) == nullptr);
            REQUIRE(orderOf<GuardOrder>(action) != nullptr);

            f.sim.unitDefinitions.at("peewee").canGuard = false;
            auto withoutGuard = f.act(DefaultActionScheme::RightClickDefault, peewee, hurt);
            REQUIRE(isMove(withoutGuard));

            // The cursor in this scheme is feedback and answers cursorselect
            // over one of your own whatever the order turns out to be; the
            // ladder's own cursor is what the MOVE button shows.
            REQUIRE(withoutGuard.cursor == CursorType::Select);
            REQUIRE(f.act(DefaultActionScheme::MoveButton, peewee, hurt).cursor == CursorType::Move);
        }

        SECTION("a Construction Kbot over a damaged friendly repairs it")
        {
            auto kbot = f.spawn("conkbot", f.player, 0_ss);
            auto hurt = f.spawn("peewee", f.player, 100_ss);
            f.sim.getUnitState(hurt).hitPoints = 50;

            auto action = f.act(DefaultActionScheme::RightClickDefault, kbot, hurt);
            REQUIRE(orderOf<RepairOrder>(action) != nullptr);
            REQUIRE(orderOf<RepairOrder>(action)->target == hurt);

            auto armed = f.act(DefaultActionScheme::MoveButton, kbot, hurt);
            REQUIRE(orderOf<RepairOrder>(armed) != nullptr);
            REQUIRE(armed.cursor == CursorType::Repair);
        }

        SECTION("a Commander over an enemy captures it")
        {
            // Capture is the first arm of the ladder, above reclaim and above
            // the attack arm RWE keeps, so a Commander with cancapture takes
            // the unit rather than shooting it.
            auto commander = f.spawn("commander", f.player, 0_ss);
            auto target = f.spawn("peewee", f.enemy, 100_ss);

            auto action = f.act(DefaultActionScheme::RightClickDefault, commander, target);
            REQUIRE(orderOf<CaptureOrder>(action) != nullptr);
            REQUIRE(orderOf<CaptureOrder>(action)->target == target);
            REQUIRE(action.cursor == CursorType::Red);

            // With the MOVE button armed the same ladder runs, and there the
            // cursor is the ladder's own rather than the red/green feedback.
            auto armed = f.act(DefaultActionScheme::MoveButton, commander, target);
            REQUIRE(orderOf<CaptureOrder>(armed) != nullptr);
            REQUIRE(armed.cursor == CursorType::Capture);
        }

        SECTION("a fighter over a friendly repair pad lands on it")
        {
            auto fighter = f.spawn("fighter", f.player, 0_ss);
            auto pad = f.spawn("pad", f.player, 100_ss);
            f.sim.getUnitState(pad).activated = true;

            auto action = f.act(DefaultActionScheme::MoveButton, fighter, pad);
            REQUIRE(orderOf<LandOnAirBaseOrder>(action) != nullptr);
            REQUIRE(orderOf<LandOnAirBaseOrder>(action)->target == pad);

            // 0x43EA83 gives cursorunload for exactly this pair.
            REQUIRE(action.cursor == CursorType::Unload);
        }

        SECTION("a unit never orders itself, but still says what the cursor should be")
        {
            auto peewee = f.spawn("peewee", f.player, 0_ss);

            auto action = f.act(DefaultActionScheme::LeftClickDefault, peewee, peewee);
            REQUIRE(std::holds_alternative<DefaultActionNothing>(action.action));
            REQUIRE(action.cursor == CursorType::Select);
        }

        SECTION("a click on nothing at all is a move")
        {
            auto peewee = f.spawn("peewee", f.player, 0_ss);

            auto action = computeDefaultAction(f.sim, DefaultActionScheme::LeftClickDefault, peewee, std::nullopt, std::nullopt);
            REQUIRE(isMove(action));
            REQUIRE(action.cursor == CursorType::Move);
        }

        SECTION("a factory takes a rally point from the MOVE button and from no click")
        {
            auto factory = f.spawn("factory", f.player, 0_ss);
            auto ground = [&](DefaultActionScheme scheme) {
                return computeDefaultAction(f.sim, scheme, factory, std::nullopt, std::nullopt);
            };

            REQUIRE(isMove(ground(DefaultActionScheme::MoveButton)));

            auto left = ground(DefaultActionScheme::LeftClickDefault);
            REQUIRE(std::holds_alternative<DefaultActionNothing>(left.action));
            REQUIRE(left.cursor == CursorType::Move);

            REQUIRE(std::holds_alternative<DefaultActionNothing>(ground(DefaultActionScheme::RightClickDefault).action));

            auto peewee = f.spawn("peewee", f.enemy, 100_ss);
            REQUIRE(std::holds_alternative<DefaultActionNothing>(f.act(DefaultActionScheme::RightClickDefault, factory, peewee).action));
        }
    }

    TEST_CASE("a teammate's unit is an ally, not an enemy", "[defaultaction]")
    {
        // 0x43F0EC-0x43F12D reads the ally table, not the owner, so a unit
        // belonging to a player on the same team takes the allied arms of
        // the ladder: no capture, no reclaim, no attack, and the feedback
        // cursor is the green one an ally gets rather than the red one.
        Fixture f;
        auto ally = addPlayer(f.sim, "friend");
        f.sim.getPlayer(f.player).teamId = 1;
        f.sim.getPlayer(ally).teamId = 1;

        auto commander = f.spawn("commander", f.player, 0_ss);
        auto theirs = f.spawn("peewee", ally, 100_ss);
        auto enemys = f.spawn("peewee", f.enemy, 200_ss);
        // At full health, so the builder's repair arm (also an allied arm)
        // does not answer before the guard arm this case is about.
        f.sim.getUnitState(theirs).hitPoints = 200;

        SECTION("right-click on a teammate's unit guards it, where the same click on an enemy's captures it")
        {
            auto onAlly = f.act(DefaultActionScheme::RightClickDefault, commander, theirs);
            REQUIRE(orderOf<CaptureOrder>(onAlly) == nullptr);
            REQUIRE(orderOf<AttackOrder>(onAlly) == nullptr);
            REQUIRE(orderOf<GuardOrder>(onAlly) != nullptr);
            REQUIRE(onAlly.cursor == CursorType::Green);

            auto onEnemy = f.act(DefaultActionScheme::RightClickDefault, commander, enemys);
            REQUIRE(orderOf<CaptureOrder>(onEnemy) != nullptr);
            REQUIRE(onEnemy.cursor == CursorType::Red);
        }

        SECTION("left-click on a teammate's unit does not attack it")
        {
            auto onAlly = f.act(DefaultActionScheme::LeftClickDefault, commander, theirs);
            REQUIRE(orderOf<AttackOrder>(onAlly) == nullptr);
            REQUIRE(orderOf<ReclaimOrder>(onAlly) == nullptr);

            auto onEnemy = f.act(DefaultActionScheme::LeftClickDefault, commander, enemys);
            REQUIRE(orderOf<AttackOrder>(onEnemy) != nullptr);
        }

        SECTION("a transport still only loads its own units")
        {
            auto atlas = f.spawn("atlas", f.player, 300_ss);
            auto onAlly = f.act(DefaultActionScheme::RightClickDefault, atlas, theirs);
            REQUIRE(orderOf<LoadOrder>(onAlly) == nullptr);
        }
    }

    TEST_CASE("a mixed selection shows the lowest-numbered cursor", "[defaultaction]")
    {
        // 0x48D3E9 keeps the minimum over the selection, so the most specific
        // thing anything selected could do is what the player is shown.
        REQUIRE(preferredCursor(CursorType::Move, CursorType::Attack) == CursorType::Attack);
        REQUIRE(preferredCursor(CursorType::Normal, CursorType::Select) == CursorType::Select);
        REQUIRE(preferredCursor(CursorType::Pickup, CursorType::Load) == CursorType::Pickup);
        REQUIRE(preferredCursor(CursorType::Guard, CursorType::Repair) == CursorType::Guard);
        REQUIRE(originalCursorId(CursorType::Pickup) == 8);
        REQUIRE(originalCursorId(CursorType::Load) == 12);
    }
}
