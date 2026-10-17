#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/game/DefaultAction.h>
#include <rwe/sim/UnitBehaviorService_util.h>
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

        /**
         * ARMCARRY, from its FBI: Builder=1, canmove=1, canguard=1 and no
         * CanReclamate, one of the 27 shipped builders that the original's
         * repair predicate (0x4899B0) does not admit. BT-62.
         */
        UnitDefinition makeCarrierDef()
        {
            auto d = makeMobileDef();
            d.builder = true;
            d.canFly = false;
            return d;
        }

        /** ARMCA: Builder, CanFly, CanReclamate, MaxWaterDepth=0. */
        UnitDefinition makeConstructionAircraftDef()
        {
            auto d = makeMobileDef();
            d.builder = true;
            d.canReclamate = true;
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
                sim.unitDefinitions["carrier"] = makeCarrierDef();
                sim.unitDefinitions["conaircraft"] = makeConstructionAircraftDef();
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

        SECTION("a factory or a carrier over a damaged friendly gets no repair, though both are builders")
        {
            // BT-62: the original keys repair and help-build on the copy of
            // canreclamate (flags2 bit 9), not on Builder, and ARMLAB and
            // ARMCARRY are among the 27 shipped builders without it.
            auto hurt = f.spawn("peewee", f.player, 100_ss);
            f.sim.getUnitState(hurt).hitPoints = 50;

            for (const auto* type : {"factory", "carrier"})
            {
                auto builder = f.spawn(type, f.player, 0_ss);
                for (auto scheme : {DefaultActionScheme::RightClickDefault, DefaultActionScheme::MoveButton, DefaultActionScheme::LeftClickDefault})
                {
                    auto action = f.act(scheme, builder, hurt);
                    REQUIRE(orderOf<RepairOrder>(action) == nullptr);
                    REQUIRE(orderOf<CompleteBuildOrder>(action) == nullptr);
                }
            }
        }

        SECTION("a Construction Kbot leaves a full-health friendly alone")
        {
            auto kbot = f.spawn("conkbot", f.player, 0_ss);
            auto sound = f.spawn("peewee", f.player, 100_ss);
            f.sim.getUnitState(sound).hitPoints = 200;
            auto action = f.act(DefaultActionScheme::MoveButton, kbot, sound);
            REQUIRE(orderOf<RepairOrder>(action) == nullptr);
        }

        SECTION("a Construction Kbot does not repair an airborne friendly")
        {
            auto kbot = f.spawn("conkbot", f.player, 0_ss);
            auto hurt = f.spawn("fighter", f.player, 100_ss);
            f.sim.getUnitState(hurt).hitPoints = 50;
            REQUIRE(orderOf<RepairOrder>(f.act(DefaultActionScheme::MoveButton, kbot, hurt)) != nullptr);

            f.sim.getUnitState(hurt).physics = UnitPhysicsInfoAir();
            REQUIRE(orderOf<RepairOrder>(f.act(DefaultActionScheme::MoveButton, kbot, hurt)) == nullptr);
        }

        SECTION("the depth test: a ground builder wades to its maximum water depth, a flier needs the target out of the water")
        {
            // ARMCK has MaxWaterDepth=25 and ARMCA flies. The sea is at zero
            // here and the test model is ten tall, so a target at height -20
            // has its top at -10: inside the Kbot's 25 and under the sea.
            std::get<UnitDefinition::AdHocMovementClass>(f.sim.unitDefinitions.at("conkbot").movementCollisionInfo).maxWaterDepth = 25u;
            auto kbot = f.spawn("conkbot", f.player, 0_ss);
            auto aircraft = f.spawn("conaircraft", f.player, 50_ss);
            auto hurt = f.spawn("peewee", f.player, 100_ss);
            f.sim.getUnitState(hurt).hitPoints = 50;

            f.sim.getUnitState(hurt).position.y = -20_ss;
            REQUIRE(orderOf<RepairOrder>(f.act(DefaultActionScheme::MoveButton, kbot, hurt)) != nullptr);
            REQUIRE(orderOf<RepairOrder>(f.act(DefaultActionScheme::MoveButton, aircraft, hurt)) == nullptr);

            f.sim.getUnitState(hurt).position.y = -40_ss;
            REQUIRE(orderOf<RepairOrder>(f.act(DefaultActionScheme::MoveButton, kbot, hurt)) == nullptr);

            f.sim.getUnitState(hurt).position.y = 0_ss;
            REQUIRE(orderOf<RepairOrder>(f.act(DefaultActionScheme::MoveButton, aircraft, hurt)) != nullptr);
        }

        SECTION("a Commander right-clicking an enemy attacks it, and only the MOVE button captures")
        {
            // BT-61: the right-click chain at 0x43FA00 opens with the attack
            // arm and has no capture arm (cancapture, bit 12, is never
            // tested there). Capture is command 2's ladder, the MOVE
            // button's, and the armed CAPTURE button.
            auto commander = f.spawn("commander", f.player, 0_ss);
            auto target = f.spawn("peewee", f.enemy, 100_ss);

            auto action = f.act(DefaultActionScheme::RightClickDefault, commander, target);
            REQUIRE(orderOf<AttackOrder>(action) != nullptr);
            REQUIRE(std::get<UnitId>(orderOf<AttackOrder>(action)->target) == target);
            REQUIRE(orderOf<CaptureOrder>(action) == nullptr);
            REQUIRE(action.cursor == CursorType::Red);

            // With the MOVE button armed the other ladder runs, and there the
            // cursor is the ladder's own rather than the red/green feedback.
            auto armed = f.act(DefaultActionScheme::MoveButton, commander, target);
            REQUIRE(orderOf<CaptureOrder>(armed) != nullptr);
            REQUIRE(armed.cursor == CursorType::Capture);
        }

        SECTION("a unit that can reclaim but not attack reclaims an enemy on a right-click")
        {
            // The arm an earlier reading called capture: canreclamate, tested
            // after the attack arm.
            auto kbot = f.spawn("conkbot", f.player, 0_ss);
            auto target = f.spawn("peewee", f.enemy, 100_ss);

            auto action = f.act(DefaultActionScheme::RightClickDefault, kbot, target);
            REQUIRE(orderOf<ReclaimOrder>(action) != nullptr);
            REQUIRE(std::get<UnitId>(orderOf<ReclaimOrder>(action)->target) == target);
        }

        SECTION("a Commander right-clicking a reclaimable feature reclaims it, behind the unit arms")
        {
            FeatureDefinition tree{};
            tree.name = "tree";
            tree.footprintX = 1;
            tree.footprintZ = 1;
            tree.height = 1_ss;
            tree.reclaimable = true;
            auto treeId = f.sim.featureDefinitions.insert(tree);
            auto feature = f.sim.addFeature(treeId, 4, 4);
            REQUIRE(feature.has_value());

            auto commander = f.spawn("commander", f.player, 0_ss);
            auto action = computeDefaultAction(f.sim, DefaultActionScheme::RightClickDefault, commander, std::nullopt, feature);
            REQUIRE(orderOf<ReclaimOrder>(action) != nullptr);

            // A friendly peewee standing over the wreck: the unit arms (guard)
            // answer first, as in the original's chain.
            auto friendly = f.spawn("peewee", f.player, 100_ss);
            f.sim.getUnitState(friendly).hitPoints = 200;
            auto over = computeDefaultAction(f.sim, DefaultActionScheme::RightClickDefault, commander, friendly, feature);
            REQUIRE(orderOf<GuardOrder>(over) != nullptr);
        }

        SECTION("a Commander right-clicking a damaged ally repairs it and a sound one is guarded")
        {
            auto commander = f.spawn("commander", f.player, 0_ss);
            auto ally = f.spawn("peewee", f.player, 100_ss);

            f.sim.getUnitState(ally).hitPoints = 50;
            REQUIRE(orderOf<RepairOrder>(f.act(DefaultActionScheme::RightClickDefault, commander, ally)) != nullptr);

            f.sim.getUnitState(ally).hitPoints = 200;
            REQUIRE(orderOf<RepairOrder>(f.act(DefaultActionScheme::RightClickDefault, commander, ally)) == nullptr);
            REQUIRE(orderOf<GuardOrder>(f.act(DefaultActionScheme::RightClickDefault, commander, ally)) != nullptr);
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

        SECTION("right-click on a teammate's unit guards it, where the same click on an enemy's attacks it")
        {
            auto onAlly = f.act(DefaultActionScheme::RightClickDefault, commander, theirs);
            REQUIRE(orderOf<CaptureOrder>(onAlly) == nullptr);
            REQUIRE(orderOf<AttackOrder>(onAlly) == nullptr);
            REQUIRE(orderOf<GuardOrder>(onAlly) != nullptr);
            REQUIRE(onAlly.cursor == CursorType::Green);

            auto onEnemy = f.act(DefaultActionScheme::RightClickDefault, commander, enemys);
            REQUIRE(orderOf<AttackOrder>(onEnemy) != nullptr);
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

    namespace
    {
        /**
         * A sea map: every cell is water, the surface at 20. The ships carry
         * the weapon slots of the shipped FBIs: ARMROY and CORROY have the
         * gun in Weapon1 (ARM_ROY, range 660) and the depth charge in Weapon2
         * (ARMDEPTHCHARGE, range 410, WaterWeapon=1); ARMSUB has the torpedo
         * (ARM_TORPEDO, range 400, a water weapon) in Weapon1.
         */
        struct SeaFixture
        {
            GameSimulation sim;
            PlayerId player;
            PlayerId enemy;
            std::shared_ptr<CobScript> script;

            SeaFixture() : sim(MapTerrain(Grid<unsigned char>(64, 64, static_cast<unsigned char>(0)), 20_ss), 0u, 0, 0),
                           player(addPlayer(sim, "me")),
                           enemy(addPlayer(sim, "them")),
                           script(makeEmptyCobScript({"base"}))
            {
                registerModel(sim);

                WeaponDefinition gun{};
                gun.maxRange = 660_ss;
                sim.weaponDefinitions["ARM_ROY"] = gun;

                WeaponDefinition charge{};
                charge.maxRange = 410_ss;
                charge.waterWeapon = true;
                sim.weaponDefinitions["ARMDEPTHCHARGE"] = charge;

                WeaponDefinition torpedo{};
                torpedo.maxRange = 400_ss;
                torpedo.waterWeapon = true;
                sim.weaponDefinitions["ARM_TORPEDO"] = torpedo;

                // ARMROY: Floater, CanAttack, Weapon1 + Weapon2.
                auto ship = makeMobileDef();
                ship.canAttack = true;
                ship.floater = true;
                sim.unitDefinitions["ARMROY"] = ship;
                sim.unitDefinitions["CORROY"] = ship;
                sim.unitDefinitions["ARMCRUS"] = ship;
                sim.unitDefinitions["CORCRUS"] = ship;

                // ARMSUB: WaterLine=20, so the origin sits at the surface minus 20.
                auto sub = makeMobileDef();
                sub.canAttack = true;
                sim.unitDefinitions["ARMSUB"] = sub;
                sim.unitDefinitions["CORSUB"] = sub;

                // ARMPW stands in for any unit with a gun and no depth charge.
                sim.unitDefinitions["peewee"] = makePeeweeDef();
                // A hovercraft with a gun and a depth charge, which no shipped unit is.
                auto hover = ship;
                hover.floater = false;
                hover.canHover = true;
                sim.unitDefinitions["hovership"] = hover;
            }

            DefaultAction act(DefaultActionScheme scheme, UnitId orderer, UnitId hovered)
            {
                return computeDefaultAction(sim, scheme, orderer, hovered, std::nullopt);
            }

            UnitId spawn(const std::string& unitType, PlayerId owner, SimScalar x, SimScalar y, std::initializer_list<const char*> weapons)
            {
                auto id = addUnitOfType(sim, unitType, owner, SimVector(x, y, 0_ss), script);
                std::size_t slot = 0;
                for (const auto* weaponType : weapons)
                {
                    UnitWeapon weapon;
                    weapon.weaponType = weaponType;
                    sim.getUnitState(id).weapons[slot++] = weapon;
                }
                return id;
            }
        };
    }

    TEST_CASE("an attack click counts both of the first two weapon slots against a submerged target", "[defaultaction][attackeligibility]")
    {
        SeaFixture f;

        // ARMSUB and CORSUB ride 20 below the surface; the test model is ten
        // tall, so the top is at -10 and the target is under the sea.
        auto submerged = f.spawn("ARMSUB", f.enemy, 500_ss, 0_ss, {"ARM_TORPEDO"});
        auto surfaced = f.spawn("ARMSUB", f.enemy, 600_ss, 20_ss, {"ARM_TORPEDO"});

        auto acts = [&](UnitId orderer, UnitId target) {
            return f.act(DefaultActionScheme::LeftClickDefault, orderer, target);
        };

        SECTION("the Crusader and the Enforcer, depth charge in Weapon2, accept it")
        {
            for (const auto* type : {"ARMROY", "CORROY", "ARMCRUS", "CORCRUS"})
            {
                auto ship = f.spawn(type, f.player, 0_ss, 20_ss, {"ARM_ROY", "ARMDEPTHCHARGE"});
                auto action = acts(ship, submerged);
                REQUIRE(orderOf<AttackOrder>(action) != nullptr);
                REQUIRE(action.cursor == CursorType::Attack);
                REQUIRE(attackOrderIsAccepted(f.sim, f.sim.unitDefinitions.at(type), f.sim.getUnitState(ship), f.sim.getUnitState(submerged)));
            }
        }

        SECTION("a submarine with a torpedo in Weapon1 accepts it")
        {
            auto sub = f.spawn("ARMSUB", f.player, 0_ss, 0_ss, {"ARM_TORPEDO"});
            REQUIRE(orderOf<AttackOrder>(acts(sub, submerged)) != nullptr);
        }

        SECTION("a unit with no water weapon in either slot gets no order, and the ladder stops there")
        {
            for (const auto* type : {"peewee", "ARMROY"})
            {
                auto gunOnly = f.spawn(type, f.player, 0_ss, 20_ss, {"ARM_ROY"});
                for (auto scheme : {DefaultActionScheme::LeftClickDefault, DefaultActionScheme::RightClickDefault, DefaultActionScheme::MoveButton})
                {
                    auto action = f.act(scheme, gunOnly, submerged);
                    REQUIRE(orderOf<AttackOrder>(action) == nullptr);
                    REQUIRE(std::holds_alternative<DefaultActionNothing>(action.action));
                }
                REQUIRE_FALSE(attackOrderIsAccepted(f.sim, f.sim.unitDefinitions.at(type), f.sim.getUnitState(gunOnly), f.sim.getUnitState(submerged)));
            }
        }

        SECTION("the third slot is never consulted")
        {
            auto ship = f.spawn("ARMROY", f.player, 0_ss, 20_ss, {"ARM_ROY"});
            UnitWeapon third;
            third.weaponType = "ARMDEPTHCHARGE";
            f.sim.getUnitState(ship).weapons[2] = third;
            REQUIRE(orderOf<AttackOrder>(acts(ship, submerged)) == nullptr);
        }

        SECTION("a surfaced target needs no water weapon")
        {
            auto gunOnly = f.spawn("ARMROY", f.player, 0_ss, 20_ss, {"ARM_ROY"});
            REQUIRE(orderOf<AttackOrder>(acts(gunOnly, surfaced)) != nullptr);
            auto charged = f.spawn("ARMROY", f.player, 100_ss, 20_ss, {"ARM_ROY", "ARMDEPTHCHARGE"});
            REQUIRE(orderOf<AttackOrder>(acts(charged, surfaced)) != nullptr);
        }

        SECTION("the hover test is the orderer's canhover: a hovercraft with a water weapon gets no order against a surfaced target")
        {
            auto hover = f.spawn("hovership", f.player, 0_ss, 20_ss, {"ARM_ROY", "ARMDEPTHCHARGE"});
            REQUIRE(orderOf<AttackOrder>(acts(hover, surfaced)) == nullptr);

            auto hoverFirst = f.spawn("hovership", f.player, 100_ss, 20_ss, {"ARMDEPTHCHARGE"});
            REQUIRE(orderOf<AttackOrder>(acts(hoverFirst, surfaced)) == nullptr);

            // Without a water weapon the same hovercraft attacks normally, and
            // a non-hover ship with the depth charge is not refused.
            auto hoverGun = f.spawn("hovership", f.player, 200_ss, 20_ss, {"ARM_ROY"});
            REQUIRE(orderOf<AttackOrder>(acts(hoverGun, surfaced)) != nullptr);
            auto ship = f.spawn("ARMROY", f.player, 300_ss, 20_ss, {"ARM_ROY", "ARMDEPTHCHARGE"});
            REQUIRE(orderOf<AttackOrder>(acts(ship, surfaced)) != nullptr);
        }
    }

    TEST_CASE("a first weapon that is anti-air makes no attack order against anything it cannot shoot", "[defaultaction][attackeligibility]")
    {
        // BT-63, 0x43F154: no enemy target (a place, or a friendly or neutral
        // unit) with the first weapon anti-air gives no order, and so does an
        // enemy that is not in the flying placement mode. Only four shipped
        // weapons set toairweapon; ARMYORK_GUN is the Phalanx's.
        SeaFixture f;

        WeaponDefinition flak{};
        flak.maxRange = 680_ss;
        flak.physicsType = ProjectilePhysicsTypeBallistic();
        flak.toAirWeapon = true;
        f.sim.weaponDefinitions["ARMYORK_GUN"] = flak;

        auto phalanx = f.spawn("peewee", f.player, 0_ss, 0_ss, {"ARMYORK_GUN"});
        auto gunTank = f.spawn("peewee", f.player, 50_ss, 0_ss, {"ARM_ROY"});
        auto tank = f.spawn("peewee", f.enemy, 500_ss, 20_ss, {"ARM_ROY"});
        auto friend_ = f.spawn("peewee", f.player, 600_ss, 20_ss, {"ARM_ROY"});
        f.sim.unitDefinitions["fighter"] = makeFighterDef();
        auto plane = f.spawn("fighter", f.enemy, 700_ss, 100_ss, {"ARM_ROY"});
        f.sim.getUnitState(plane).physics = UnitPhysicsInfoAir();

        auto accepted = [&](UnitId orderer, UnitId target) {
            return attackOrderIsAccepted(f.sim, f.sim.unitDefinitions.at(f.sim.getUnitState(orderer).unitType), f.sim.getUnitState(orderer), f.sim.getUnitState(target));
        };

        SECTION("an enemy on the ground is refused to an anti-air first weapon")
        {
            REQUIRE_FALSE(accepted(phalanx, tank));
            REQUIRE(orderOf<AttackOrder>(f.act(DefaultActionScheme::LeftClickDefault, phalanx, tank)) == nullptr);
            REQUIRE(accepted(gunTank, tank));
        }

        SECTION("an enemy in the air is accepted")
        {
            REQUIRE(accepted(phalanx, plane));
        }

        SECTION("a friendly unit is no enemy target, so an anti-air first weapon is refused it too")
        {
            REQUIRE_FALSE(accepted(phalanx, friend_));
            REQUIRE(accepted(gunTank, friend_));
        }

        SECTION("a place is refused to an anti-air first weapon and not to a gun")
        {
            REQUIRE_FALSE(attackGroundOrderIsAccepted(f.sim, f.sim.getUnitState(phalanx)));
            REQUIRE(attackGroundOrderIsAccepted(f.sim, f.sim.getUnitState(gunTank)));
        }

        SECTION("only the first weapon counts")
        {
            auto gunFirst = f.spawn("peewee", f.player, 100_ss, 0_ss, {"ARM_ROY", "ARMYORK_GUN"});
            REQUIRE(accepted(gunFirst, tank));
            REQUIRE(attackGroundOrderIsAccepted(f.sim, f.sim.getUnitState(gunFirst)));
        }
    }
}
