#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/LosTables.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitFireOrders.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitPieceDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>
#include <vector>

/**
 * Guard_NoMove (BT-91, 0x4021F0), the default mission of the armed towers.
 *
 * It is not only "free the weapons and never go looking": it adopts the target
 * of weapon slot 0 for every slot, gives up on it after three to five passes in
 * which nothing was fired, restarts with a draw of 80 in 100, and after losing
 * a target looks for another within 640 of where it stood.
 *
 * BT-91, docs/BYTE-TACTICS-SPEC-ROOM-FINDINGS.md on branch spec-room-findings;
 * static byte-tactics reading at 97d7ac59, not executed against the original.
 */
namespace rwe
{
    namespace
    {
        UnitDefinition makeTowerDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = false;
            d.canAttack = true;
            d.maxHitPoints = 1000;
            d.buildTime = 0u;
            d.sightDistance = 600u; // the LOS tables cap it at 8 cells of 32, 256 units
            d.defaultMission = DefaultMission::GuardNoMove;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            d.yardMap = Grid<YardMapCell>(2, 2, YardMapCell::Ground);
            return d;
        }

        UnitDefinition makeEnemyDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.shootMe = true;
            d.sightDistance = 100u;
            d.maxHitPoints = 100000;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId spawnUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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
            return sim.tryAddUnit(std::move(unit)).value();
        }

        struct Tower
        {
            std::shared_ptr<CobScript> script = makeEmptyCobScript({"base"});
            GameSimulation sim{makeFlatTerrain(128, 128), 0u, 0, 0};
            PlayerId us;
            PlayerId them;
            UnitId tower{0};

            explicit Tower(bool unaffordable = false)
            {
                us = addPlayer(sim, "us");
                them = addPlayer(sim, "them");
                std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
                sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

                WeaponDefinition laser{};
                laser.maxRange = 160_ss;
                laser.reloadTime = 1_ss;
                laser.burst = 1;
                laser.damage["DEFAULT"] = 10u;
                if (unaffordable)
                {
                    // A weapon that cannot pay for its shot never fires, so
                    // its reload never starts and every pass is a quiet one.
                    laser.energyPerShot = Energy(1.0e7f);
                }
                sim.weaponDefinitions["LASER"] = laser;

                sim.unitDefinitions["TOWER"] = makeTowerDef();
                sim.unitDefinitions["ENEMY"] = makeEnemyDef();
                sim.unitScriptDefinitions["TOWER"] = *script;
                sim.unitScriptDefinitions["ENEMY"] = *script;

                tower = spawnUnit(sim, "TOWER", us, SimVector(500_ss, 0_ss, 500_ss), script);
                auto& state = sim.getUnitState(tower);
                state.fireOrders = UnitFireOrders::FireAtWill;
                for (unsigned int i = 0; i < 2; ++i)
                {
                    UnitWeapon weapon;
                    weapon.weaponType = "LASER";
                    state.weapons[i] = weapon;
                }
            }

            UnitId enemyAt(float x, float z)
            {
                return spawnUnit(sim, "ENEMY", them, SimVector(SimScalar(x), 0_ss, SimScalar(z)), script);
            }

            UnitState& state() { return sim.getUnitState(tower); }

            UnitId weaponTarget(unsigned int slot)
            {
                const auto* attacking = std::get_if<UnitWeaponStateAttacking>(&state().weapons[slot]->state);
                REQUIRE(attacking != nullptr);
                return std::get<UnitId>(attacking->target);
            }
        };
    }

    TEST_CASE("a guard tower aims every weapon at the target of slot 0 (BT-91)", "[guardnomove][bt91]")
    {
        Tower t;
        // Two enemies the same distance away, so the slots' own scans could
        // each pick a different one.
        auto a = t.enemyAt(500.0f, 650.0f);
        auto b = t.enemyAt(650.0f, 500.0f);

        unsigned int heldTicks = 0;
        for (int tick = 0; tick < 200; ++tick)
        {
            t.sim.tick();
            if (t.state().guardPhase == 2)
            {
                ++heldTicks;
                REQUIRE(t.state().guardTarget.has_value());
                CHECK(t.weaponTarget(0) == *t.state().guardTarget);
                CHECK(t.weaponTarget(1) == *t.state().guardTarget);
                CHECK((*t.state().guardTarget == a || *t.state().guardTarget == b));
                REQUIRE(t.state().guardPosition.has_value());
            }
        }
        CHECK(heldTicks > 30);
    }

    TEST_CASE("a guard tower gives up a quiet target after three to five passes (BT-91)", "[guardnomove][bt91]")
    {
        Tower t(true);
        t.enemyAt(500.0f, 650.0f);

        // The tower cannot pay for a shot, so nothing is ever fired: every
        // pass counts. The limit is 3 plus 0 to 2, so the target is let go on the
        // fifth to seventh pass at the latest, passes being thirty ticks apart.
        unsigned int adoptedAt = 0;
        unsigned int leftAt = 0;
        for (unsigned int tick = 1; tick < 600 && leftAt == 0; ++tick)
        {
            t.sim.tick();
            if (adoptedAt == 0 && t.state().guardPhase == 2)
            {
                adoptedAt = tick;
                CHECK(t.state().guardLimit >= 3u);
                CHECK(t.state().guardLimit <= 5u);
            }
            if (adoptedAt != 0 && t.state().guardPhase != 2)
            {
                leftAt = tick;
            }
        }
        REQUIRE(adoptedAt != 0);
        REQUIRE(leftAt != 0);
        const auto passes = (leftAt - adoptedAt) / 30u;
        CHECK(passes >= 4u);
        CHECK(passes <= 6u);
    }

    TEST_CASE("a guard tower that loses its target looks within 640 of where it stood (BT-91)", "[guardnomove][bt91]")
    {
        Tower t;
        auto first = t.enemyAt(500.0f, 650.0f);
        // Out of the tower's reach (160) but in its sight, and inside 640 of
        // the first: only the recovery search can pick this one.
        auto second = t.enemyAt(650.0f, 600.0f);

        // The first is the only thing in the tower's range.
        for (int tick = 0; tick < 120 && t.state().guardPhase != 2; ++tick)
        {
            t.sim.tick();
        }
        REQUIRE(t.state().guardPhase == 2);
        REQUIRE(t.state().guardTarget == first);

        t.sim.getUnitState(first).markAsDeadNoCorpse();
        std::optional<UnitId> recovered;
        for (int tick = 0; tick < 40 && !recovered; ++tick)
        {
            t.sim.tick();
            if (t.state().guardTarget && *t.state().guardTarget != first)
            {
                recovered = t.state().guardTarget;
            }
        }
        REQUIRE(recovered.has_value());
        CHECK(*recovered == second);
    }

    TEST_CASE("a guard tower with nothing in sight restarts rather than recovering (BT-91)", "[guardnomove][bt91]")
    {
        Tower t;
        auto only = t.enemyAt(500.0f, 650.0f);
        for (int tick = 0; tick < 120 && t.state().guardPhase != 2; ++tick)
        {
            t.sim.tick();
        }
        REQUIRE(t.state().guardPhase == 2);

        t.sim.getUnitState(only).markAsDeadNoCorpse();
        for (int tick = 0; tick < 5; ++tick)
        {
            t.sim.tick();
        }
        CHECK_FALSE(t.state().guardTarget.has_value());
        CHECK(t.state().guardPhase != 2);
        CHECK(t.state().guardPhase != 3);
    }

    TEST_CASE("an order of its own takes a guard tower off the mission (BT-91)", "[guardnomove][bt91]")
    {
        Tower t;
        auto enemy = t.enemyAt(500.0f, 650.0f);
        for (int tick = 0; tick < 120 && t.state().guardPhase != 2; ++tick)
        {
            t.sim.tick();
        }
        REQUIRE(t.state().guardPhase == 2);

        t.state().orders.push_back(createAttackOrder(enemy));
        t.sim.tick();
        CHECK(t.state().guardPhase == 0);
        CHECK_FALSE(t.state().guardTarget.has_value());
    }
}
