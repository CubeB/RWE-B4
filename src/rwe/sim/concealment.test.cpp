#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>
#include <string>

namespace rwe
{
    namespace
    {
        MapTerrain makeConcealmentTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addConcealmentPlayer(GameSimulation& sim, const std::string& name)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(100000.0f),
                Energy(100000.0f),
                Metal(100000.0f),
                Energy(100000.0f),
                Metal(100000.0f),
                Energy(100000.0f),
            };
            return sim.addPlayer(p);
        }

        std::shared_ptr<CobScript> makeConcealmentScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        void registerConcealmentModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /**
         * Everything the tests below vary about a unit definition. The rest is
         * the same for all of them: a one tile footprint, no build time, and a
         * storage large enough that the once-a-second storage recount does not
         * clamp the owner's energy away.
         */
        struct UnitSpec
        {
            unsigned int sightDistance{0};
            unsigned int radarDistance{0};
            unsigned int sonarDistance{0};
            unsigned int radarDistanceJam{0};
            unsigned int sonarDistanceJam{0};
            bool stealth{false};
            bool onOffable{false};
            bool cloakable{false};
            float cloakCost{0.0f};
            float cloakCostMoving{0.0f};
            unsigned int minCloakDistance{80};
        };

        void defineUnit(GameSimulation& sim, const std::string& type, const UnitSpec& spec)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.canAttack = true;
            d.maxHitPoints = 1000000;
            d.buildTime = 0u;
            d.shootMe = true;
            d.category = "CORE LEVEL1 NOTAIR NOTSUB";
            d.energyStorage = Energy(100000.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};

            d.sightDistance = spec.sightDistance;
            d.radarDistance = spec.radarDistance;
            d.sonarDistance = spec.sonarDistance;
            d.radarDistanceJam = spec.radarDistanceJam;
            d.sonarDistanceJam = spec.sonarDistanceJam;
            d.stealth = spec.stealth;
            d.onOffable = spec.onOffable;
            d.cloakable = spec.cloakable;
            d.cloakCost = Energy(spec.cloakCost);
            d.cloakCostMoving = Energy(spec.cloakCostMoving);
            d.minCloakDistance = spec.minCloakDistance;

            sim.unitDefinitions[type] = d;
        }

        UnitId spawn(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& position, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = type;
            unit.owner = owner;
            unit.position = position;
            unit.previousPosition = position;
            unit.hitPoints = sim.unitDefinitions.at(type).maxHitPoints;
            unit.fireOrders = UnitFireOrders::FireAtWill;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        void tickTo(GameSimulation& sim, unsigned int targetTime)
        {
            while (sim.gameTime.value < targetTime)
            {
                sim.tick();
            }
        }

        /** The first tick on which the once-a-second economy runs. */
        constexpr unsigned int FirstEconomyTick = static_cast<unsigned int>(SimTicksPerSecond);
    }

    TEST_CASE("a radar jammer hides what is inside it", "[concealment]")
    {
        // The original's jammer erases contacts rather than producing false
        // ones, and it does it in the pass that has already filled the radar
        // picture in, so jamming always wins over the dish that found the unit.
        auto script = makeConcealmentScript();
        GameSimulation sim(makeConcealmentTerrain(), 0u, 0, 0);
        auto us = addConcealmentPlayer(sim, "us");
        auto them = addConcealmentPlayer(sim, "them");
        registerConcealmentModel(sim);

        // The dish is blind: everything it knows, it knows by radar.
        defineUnit(sim, "dish", UnitSpec{.radarDistance = 300u});
        defineUnit(sim, "target", UnitSpec{});
        defineUnit(sim, "jammer", UnitSpec{.radarDistanceJam = 100u, .onOffable = true});
        defineUnit(sim, "sonarJammer", UnitSpec{.sonarDistanceJam = 100u, .onOffable = true});

        auto targetPosition = SimVector(200_ss, 0_ss, 0_ss);
        auto jammerPosition = SimVector(240_ss, 0_ss, 0_ss);

        spawn(sim, "dish", us, SimVector(0_ss, 0_ss, 0_ss), script);
        auto targetId = spawn(sim, "target", them, targetPosition, script);

        SECTION("without a jammer the dish has it")
        {
            sim.tick();
            REQUIRE_FALSE(sim.isVisibleTo(us, targetPosition));
            REQUIRE(sim.canDetectUnit(us, targetId));
        }

        SECTION("an enemy jammer takes it off the radar, but only once switched on")
        {
            auto jammerId = spawn(sim, "jammer", them, jammerPosition, script);
            sim.tick();
            REQUIRE(sim.canDetectUnit(us, targetId));

            sim.getUnitState(jammerId).activated = true;
            sim.tick();
            REQUIRE_FALSE(sim.canDetectUnit(us, targetId));

            sim.getUnitState(jammerId).activated = false;
            sim.tick();
            REQUIRE(sim.canDetectUnit(us, targetId));
        }

        SECTION("a jammer leaves its own owner's radar picture alone")
        {
            auto jammerId = spawn(sim, "jammer", us, jammerPosition, script);
            sim.getUnitState(jammerId).activated = true;
            sim.tick();
            REQUIRE(sim.canDetectUnit(us, targetId));
        }

        SECTION("a sonar jammer says nothing about what radar can see")
        {
            auto jammerId = spawn(sim, "sonarJammer", them, jammerPosition, script);
            sim.getUnitState(jammerId).activated = true;
            sim.tick();
            REQUIRE(sim.canDetectUnit(us, targetId));
        }

        SECTION("the bubble has an edge: a unit outside it is still seen")
        {
            // A hundred and forty from the jammer, so outside its hundred, and
            // a hundred from the dish, so well inside its three hundred.
            auto farTargetId = spawn(sim, "target", them, SimVector(100_ss, 0_ss, 0_ss), script);
            auto jammerId = spawn(sim, "jammer", them, jammerPosition, script);
            sim.getUnitState(jammerId).activated = true;
            sim.tick();
            REQUIRE_FALSE(sim.canDetectUnit(us, targetId));
            REQUIRE(sim.canDetectUnit(us, farTargetId));
        }
    }

    TEST_CASE("stealth keeps a unit off radar without hiding it from the eye", "[concealment]")
    {
        auto script = makeConcealmentScript();
        GameSimulation sim(makeConcealmentTerrain(), 0u, 0, 0);
        auto us = addConcealmentPlayer(sim, "us");
        auto them = addConcealmentPlayer(sim, "them");
        registerConcealmentModel(sim);

        defineUnit(sim, "target", UnitSpec{});
        defineUnit(sim, "stealthTarget", UnitSpec{.stealth = true});

        auto targetPosition = SimVector(200_ss, 0_ss, 0_ss);

        SECTION("a stealth unit inside an enemy dish is not a contact")
        {
            defineUnit(sim, "dish", UnitSpec{.radarDistance = 300u});
            spawn(sim, "dish", us, SimVector(0_ss, 0_ss, 0_ss), script);
            auto plainId = spawn(sim, "target", them, targetPosition, script);
            auto stealthId = spawn(sim, "stealthTarget", them, SimVector(200_ss, 0_ss, 64_ss), script);

            sim.tick();

            REQUIRE(sim.canDetectUnit(us, plainId));
            REQUIRE_FALSE(sim.canDetectUnit(us, stealthId));
        }

        SECTION("but a unit that can actually see it still can")
        {
            defineUnit(sim, "watcher", UnitSpec{.sightDistance = 300u});
            spawn(sim, "watcher", us, SimVector(0_ss, 0_ss, 0_ss), script);
            auto stealthId = spawn(sim, "stealthTarget", them, targetPosition, script);

            sim.tick();

            REQUIRE(sim.isVisibleTo(us, targetPosition));
            REQUIRE(sim.canDetectUnit(us, stealthId));
        }
    }

    TEST_CASE("a cloaked unit is not acquired by the target scan", "[concealment]")
    {
        // Cloak reaches targeting through detection: RWE's scan only considers
        // what the owner can see or has on radar, so a unit that drops out of
        // canDetectUnit drops out of the scan with it.
        auto script = makeConcealmentScript();
        GameSimulation sim(makeConcealmentTerrain(), 0u, 0, 0);
        auto us = addConcealmentPlayer(sim, "us");
        auto them = addConcealmentPlayer(sim, "them");
        registerConcealmentModel(sim);

        defineUnit(sim, "shooter", UnitSpec{.sightDistance = 1000u});
        defineUnit(sim, "prey", UnitSpec{.sightDistance = 1000u, .cloakable = true, .cloakCost = 10.0f, .cloakCostMoving = 10.0f});

        WeaponDefinition w{};
        w.maxRange = 400_ss;
        w.reloadTime = 1000_ss;
        w.burst = 1;
        w.velocity = 450_ss / 30_ss;
        w.damageRadius = 4_ss;
        w.damage["DEFAULT"] = 1;
        sim.weaponDefinitions["gun"] = w;

        auto shooterId = spawn(sim, "shooter", us, SimVector(0_ss, 0_ss, 0_ss), script);
        UnitWeapon weapon;
        weapon.weaponType = "gun";
        sim.getUnitState(shooterId).weapons[0] = weapon;

        auto preyPosition = SimVector(128_ss, 0_ss, 0_ss);
        auto preyId = spawn(sim, "prey", them, preyPosition, script);

        auto currentTarget = [&]() -> std::optional<UnitId> {
            const auto& slot = sim.getUnitState(shooterId).weapons[0];
            auto attacking = std::get_if<UnitWeaponStateAttacking>(&slot->state);
            if (attacking == nullptr)
            {
                return std::nullopt;
            }
            auto target = std::get_if<UnitId>(&attacking->target);
            return target == nullptr ? std::nullopt : std::optional<UnitId>(*target);
        };

        SECTION("in plain sight it is shot at")
        {
            sim.tick();
            sim.tick();
            REQUIRE(sim.isVisibleTo(us, preyPosition));
            REQUIRE(currentTarget() == std::optional<UnitId>(preyId));
        }

        SECTION("cloaked it is passed over, in ground the shooter can see perfectly well")
        {
            sim.getUnitState(preyId).cloaked = true;
            sim.tick();
            sim.tick();
            REQUIRE(sim.isVisibleTo(us, preyPosition));
            REQUIRE_FALSE(sim.canSeeUnit(us, preyId));
            REQUIRE_FALSE(currentTarget().has_value());
        }
    }

    TEST_CASE("cloak is paid for out of energy, and costs more moving", "[concealment]")
    {
        auto script = makeConcealmentScript();
        GameSimulation sim(makeConcealmentTerrain(), 0u, 0, 0);
        auto us = addConcealmentPlayer(sim, "us");
        registerConcealmentModel(sim);

        // The Commander's numbers.
        defineUnit(sim, "commander", UnitSpec{.cloakable = true, .cloakCost = 200.0f, .cloakCostMoving = 1000.0f, .minCloakDistance = 40u});

        auto unitId = spawn(sim, "commander", us, SimVector(0_ss, 0_ss, 0_ss), script);

        SECTION("a unit that has not asked for it pays nothing and stays visible")
        {
            tickTo(sim, FirstEconomyTick);
            REQUIRE_FALSE(sim.getUnitState(unitId).cloaked);
            REQUIRE(sim.getPlayer(us).actualEnergyConsumptionBuffer.value == Catch::Approx(0.0f));
        }

        SECTION("standing still it pays CloakCost")
        {
            sim.getUnitState(unitId).cloakRequested = true;
            tickTo(sim, FirstEconomyTick);
            REQUIRE(sim.getUnitState(unitId).cloaked);
            REQUIRE(sim.getPlayer(us).actualEnergyConsumptionBuffer.value == Catch::Approx(200.0f));
        }

        SECTION("moving it pays CloakCostMoving instead")
        {
            auto& unit = sim.getUnitState(unitId);
            unit.cloakRequested = true;
            tickTo(sim, FirstEconomyTick - 1);
            // A unit is moving exactly when it has left where it was last tick,
            // which is the same test the StartMoving callback uses.
            unit.previousPosition = unit.position - SimVector(4_ss, 0_ss, 0_ss);
            sim.tick();
            REQUIRE(sim.getUnitState(unitId).cloaked);
            REQUIRE(sim.getPlayer(us).actualEnergyConsumptionBuffer.value == Catch::Approx(1000.0f));
        }

        SECTION("with the energy gone it takes nothing at all and decloaks")
        {
            sim.getUnitState(unitId).cloakRequested = true;
            sim.getPlayer(us).energy = Energy(50.0f);
            tickTo(sim, FirstEconomyTick);
            REQUIRE_FALSE(sim.getUnitState(unitId).cloaked);
            REQUIRE(sim.getPlayer(us).actualEnergyConsumptionBuffer.value == Catch::Approx(0.0f));
        }
    }

    TEST_CASE("an enemy inside MinCloakDistance breaks the cloak for three seconds", "[concealment]")
    {
        // MinCloakDistance is a proximity fuse, not a refusal to cloak: the
        // original stamps the unit with the tick ninety ahead every tick an
        // enemy is that close, so the cloak comes back three seconds after the
        // enemy leaves rather than the instant it steps out of range.
        auto script = makeConcealmentScript();
        GameSimulation sim(makeConcealmentTerrain(), 0u, 0, 0);
        auto us = addConcealmentPlayer(sim, "us");
        auto them = addConcealmentPlayer(sim, "them");
        registerConcealmentModel(sim);

        defineUnit(sim, "commander", UnitSpec{.cloakable = true, .cloakCost = 200.0f, .cloakCostMoving = 1000.0f, .minCloakDistance = 40u});
        defineUnit(sim, "scout", UnitSpec{});

        auto unitId = spawn(sim, "commander", us, SimVector(0_ss, 0_ss, 0_ss), script);
        sim.getUnitState(unitId).cloakRequested = true;
        auto scoutId = spawn(sim, "scout", them, SimVector(400_ss, 0_ss, 0_ss), script);

        tickTo(sim, FirstEconomyTick);
        REQUIRE(sim.getUnitState(unitId).cloaked);

        // Twenty units away, inside the Commander's forty.
        sim.getUnitState(scoutId).position = SimVector(20_ss, 0_ss, 0_ss);
        tickTo(sim, 2 * FirstEconomyTick);

        SECTION("standing next to it decloaks it")
        {
            REQUIRE_FALSE(sim.getUnitState(unitId).cloaked);
        }

        SECTION("and it stays decloaked for three seconds after the enemy leaves")
        {
            // The last stamp lands on tick 60, so the cloak is held off until
            // tick 150.
            sim.getUnitState(scoutId).position = SimVector(400_ss, 0_ss, 0_ss);

            tickTo(sim, 3 * FirstEconomyTick);
            REQUIRE_FALSE(sim.getUnitState(unitId).cloaked);
            tickTo(sim, 4 * FirstEconomyTick);
            REQUIRE_FALSE(sim.getUnitState(unitId).cloaked);
            tickTo(sim, 5 * FirstEconomyTick);
            REQUIRE(sim.getUnitState(unitId).cloaked);
        }

        SECTION("a friendly unit standing just as close does not")
        {
            sim.getUnitState(scoutId).position = SimVector(400_ss, 0_ss, 0_ss);
            spawn(sim, "scout", us, SimVector(0_ss, 0_ss, 20_ss), script);
            tickTo(sim, 5 * FirstEconomyTick);
            REQUIRE(sim.getUnitState(unitId).cloaked);
        }
    }
}
