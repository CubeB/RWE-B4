#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <memory>

namespace rwe
{
    namespace
    {
        MapTerrain makeKamikazeTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addKamikazePlayer(GameSimulation& sim, const std::string& name)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
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
            return sim.addPlayer(p);
        }

        std::shared_ptr<CobScript> makeKamikazeScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        void registerKamikazeModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /**
         * CORROACH, the Roach: a crawling bomb with no weapon at all, a
         * kamikazedistance of 40, and CRAWL_BLAST as its SelfDestructAs against
         * the smaller CRAWL_BLASTSML it explodes as when something else kills it.
         */
        void defineRoach(GameSimulation& sim, const std::string& type, bool kamikaze, unsigned int distance)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = true;
            d.kamikaze = kamikaze;
            d.kamikazeDistance = distance;
            d.maxVelocity = 4_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.sightDistance = 1000u;
            d.maxHitPoints = 195;
            d.buildTime = 0u;
            d.shootMe = true;
            d.explodeAs = "CRAWL_BLASTSML";
            d.selfDestructAs = "CRAWL_BLAST";
            d.category = "CORE KBOT WEAPON LEVEL2 NOTAIR NOTSUB KAMIKAZE";
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;
        }

        void defineVictim(GameSimulation& sim, const std::string& type)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.sightDistance = 1000u;
            d.maxHitPoints = 100000;
            d.buildTime = 0u;
            d.shootMe = true;
            d.category = "ARM LEVEL1 PLANT NOTAIR NOTSUB";
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;
        }

        UnitId spawnKamikazeUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = unitType;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at(unitType).maxHitPoints;
            unit.fireOrders = UnitFireOrders::FireAtWill;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        /** CRAWL_BLAST and CRAWL_BLASTSML, the two explosions a crawling bomb has. */
        void defineBlasts(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.maxRange = 0_ss;
            w.damageRadius = 96_ss;
            w.damage["DEFAULT"] = 400;
            sim.weaponDefinitions["CRAWL_BLAST"] = w;

            w.damageRadius = 48_ss;
            w.damage["DEFAULT"] = 200;
            sim.weaponDefinitions["CRAWL_BLASTSML"] = w;
        }

        std::optional<UnitDiedEvent> deathOf(const GameSimulation& sim, UnitId id)
        {
            std::optional<UnitDiedEvent> result;
            for (const auto& e : sim.events)
            {
                if (auto died = std::get_if<UnitDiedEvent>(&e); died != nullptr && died->unitId == id)
                {
                    result = *died;
                }
            }
            return result;
        }

        /** Runs the sim until `id` dies, and answers how far from `victim` it was when it did. */
        std::optional<SimScalar> runUntilDeath(GameSimulation& sim, UnitId id, UnitId victim)
        {
            auto victimPosition = sim.getUnitState(victim).position;
            for (int i = 0; i < 900 && sim.tryGetUnitState(id); ++i)
            {
                sim.tick();
            }

            auto death = deathOf(sim, id);
            if (!death)
            {
                return std::nullopt;
            }
            return death->position.distance(victimPosition);
        }
    }

    // Priority 14. `kamikaze` is bit 28 of `def+0x241` (0x42CB18) and
    // `kamikazedistance` the word at `def+0x218` (0x42CB29). An attack order on
    // such a unit becomes the ATTACK_KAMIKAZE mission (0x43F38A) whose handler
    // (0x403336) closes to max(kamikazedistance, 16) and then issues the ordinary
    // SELFDESTRUCT order (0x4032E4).
    TEST_CASE("a crawling bomb detonates on its target", "[kamikaze]")
    {
        GameSimulation sim(makeKamikazeTerrain(), 0u, 0, 0);
        registerKamikazeModel(sim);
        auto script = makeKamikazeScript();

        auto attacker = addKamikazePlayer(sim, "core");
        auto defender = addKamikazePlayer(sim, "arm");

        defineVictim(sim, "victim");
        defineBlasts(sim);

        SECTION("it walks to the target and blows itself up")
        {
            defineRoach(sim, "roach", true, 40u);

            auto victim = spawnKamikazeUnit(sim, "victim", defender, SimVector(200_ss, 0_ss, 0_ss), script);
            auto roach = spawnKamikazeUnit(sim, "roach", attacker, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(roach).addOrder(AttackOrder(victim));

            REQUIRE(runUntilDeath(sim, roach, victim).has_value());

            // It went off by its own hand, so the blast is SelfDestructAs.
            REQUIRE(deathOf(sim, roach)->deathType == UnitDiedEvent::DeathType::SelfDestructed);
        }

        SECTION("it closes to kamikazedistance before it goes off")
        {
            // Started two hundred units out with a trigger radius of forty, so
            // it has to cross most of the gap first.
            defineRoach(sim, "roach", true, 40u);

            auto victim = spawnKamikazeUnit(sim, "victim", defender, SimVector(200_ss, 0_ss, 0_ss), script);
            auto roach = spawnKamikazeUnit(sim, "roach", attacker, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(roach).addOrder(AttackOrder(victim));

            sim.tick();
            REQUIRE(sim.tryGetUnitState(roach).has_value());

            auto distanceAtDeath = runUntilDeath(sim, roach, victim);
            REQUIRE(distanceAtDeath.has_value());
            REQUIRE(*distanceAtDeath <= 40_ss);
        }

        SECTION("a longer kamikazedistance goes off further out")
        {
            // The Invader's 80 against the Roach's 40.
            defineRoach(sim, "invader", true, 80u);

            auto victim = spawnKamikazeUnit(sim, "victim", defender, SimVector(300_ss, 0_ss, 0_ss), script);
            auto invader = spawnKamikazeUnit(sim, "invader", attacker, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(invader).addOrder(AttackOrder(victim));

            auto distanceAtDeath = runUntilDeath(sim, invader, victim);
            REQUIRE(distanceAtDeath.has_value());
            REQUIRE(*distanceAtDeath <= 80_ss);
            REQUIRE(*distanceAtDeath > 40_ss);
        }

        SECTION("a weaponless unit that is not a kamikaze just drops the order")
        {
            // Which is what RWE did with the Roach before this: no weapon[0],
            // so attackTarget threw the order away and the unit sat still.
            defineRoach(sim, "dud", false, 40u);

            auto victim = spawnKamikazeUnit(sim, "victim", defender, SimVector(200_ss, 0_ss, 0_ss), script);
            auto dud = spawnKamikazeUnit(sim, "dud", attacker, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(dud).addOrder(AttackOrder(victim));

            for (int i = 0; i < 300; ++i)
            {
                sim.tick();
            }

            REQUIRE(sim.tryGetUnitState(dud).has_value());
            REQUIRE(sim.getUnitState(dud).orders.empty());
        }
    }
}
