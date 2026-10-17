#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        /**
         * `Killed(severity, corpsetype)` that explodes its one piece and asks
         * for the intact wreck, the shape of a shipped ladder's first rung.
         */
        std::shared_ptr<CobScript> makeKilledScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");

            auto& code = script->instructions;
            auto emit = [&](OpCode op) { code.push_back(static_cast<uint32_t>(op)); };
            auto operand = [&](uint32_t v) { code.push_back(v); };

            emit(OpCode::PUSH_CONSTANT);
            operand(1); // SHATTER
            emit(OpCode::EXPLODE);
            operand(0); // piece "base"
            emit(OpCode::PUSH_CONSTANT);
            operand(1);
            emit(OpCode::POP_LOCAL_VAR);
            operand(1); // corpsetype = 1
            emit(OpCode::PUSH_LOCAL_VAR);
            operand(1);
            emit(OpCode::RETURN);

            script->functions.push_back(CobFunctionInfo{"Killed", 0});
            return script;
        }

        UnitDefinition makeDef(unsigned int buildTime)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.maxHitPoints = 100;
            d.buildTime = buildTime;
            d.explodeAs = "boom";
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId spawn(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = type;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at(type).maxHitPoints;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        struct Outcome
        {
            bool pieceExploded{false};
            std::optional<UnitDiedEvent::DeathType> deathType;
            bool leavesCorpse{false};
            unsigned int bystanderHitPoints{0};
            unsigned int attackerKills{0};
            unsigned int ownerLosses{0};
        };

        /** Kill a unit of the given build time with a lethal weapon hit and report what came of it. */
        Outcome killByWeapon(unsigned int buildTime)
        {
            auto script = makeKilledScript();
            GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
            auto us = addPlayer(sim, "us");
            auto them = addPlayer(sim, "them");
            sim.unitDefinitions["victim"] = makeDef(buildTime);
            auto bystanderDef = makeDef(0u);
            bystanderDef.maxHitPoints = 1000;
            bystanderDef.explodeAs = "";
            sim.unitDefinitions["bystander"] = bystanderDef;
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

            // The unit's death weapon: a blast big enough to reach the bystander.
            WeaponDefinition boom{};
            boom.damageRadius = 60_ss;
            boom.damage["DEFAULT"] = 300;
            sim.weaponDefinitions["boom"] = boom;

            auto attacker = spawn(sim, "bystander", us, SimVector(300_ss, 0_ss, 300_ss), makeKilledScript());
            auto victim = spawn(sim, "victim", them, SimVector(100_ss, 0_ss, 100_ss), script);
            auto bystander = spawn(sim, "bystander", them, SimVector(132_ss, 0_ss, 100_ss), makeKilledScript());
            tick(sim, 1);
            sim.events.clear();

            sim.applyDamage(victim, 500u, attacker, false, std::nullopt, "weapon", false, SimVector(100_ss, 0_ss, 105_ss));

            Outcome o;
            for (const auto& e : sim.events)
            {
                if (auto died = std::get_if<UnitDiedEvent>(&e); died != nullptr && died->unitId == victim)
                {
                    o.deathType = died->deathType;
                }
                if (auto exploded = std::get_if<PieceExplodedEvent>(&e); exploded != nullptr && exploded->unitId == victim)
                {
                    o.pieceExploded = true;
                }
            }
            if (auto dead = std::get_if<UnitState::LifeStateDead>(&sim.getUnitState(victim).lifeState))
            {
                o.leavesCorpse = dead->leaveCorpse;
            }
            o.bystanderHitPoints = sim.getUnitState(bystander).hitPoints;
            o.attackerKills = sim.getUnitState(attacker).kills;
            o.ownerLosses = sim.getPlayer(them).unitsLost;
            return o;
        }
    }

    TEST_CASE("a finished unit killed by a weapon explodes, leaves a corpse and credits the attacker", "[nanoframe][death]")
    {
        // The control: everything below is measured against this.
        auto o = killByWeapon(0u);
        REQUIRE(o.pieceExploded);
        REQUIRE(o.deathType == UnitDiedEvent::DeathType::NormalExploded);
        REQUIRE(o.leavesCorpse);
        REQUIRE(o.bystanderHitPoints < 1000u);
        REQUIRE(o.attackerKills == 1u);
        REQUIRE(o.ownerLosses == 1u);
    }

    TEST_CASE("an unfinished unit killed by a weapon still runs Killed and throws its pieces, but leaves no corpse and no blast", "[nanoframe][death]")
    {
        // BT-45 (0x4864B0, 0x4866D0): the Killed script is queried exactly as
        // for a finished unit, so the pieces go; the corpse is withheld
        // (0x4865D2) and so is the death weapon (0x486D2F), and the attacker
        // earns nothing (0x4869A7) while the owner still takes the loss.
        auto o = killByWeapon(100u);
        REQUIRE(o.pieceExploded);
        REQUIRE_FALSE(o.leavesCorpse);
        REQUIRE(o.deathType == UnitDiedEvent::DeathType::Deleted);
        REQUIRE(o.bystanderHitPoints == 1000u);
        REQUIRE(o.attackerKills == 0u);
        REQUIRE(o.ownerLosses == 1u);
    }
}
