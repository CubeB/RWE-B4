#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitPieceDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>

/**
 * BT-66: the original's activation setter (0x48B090) returns at once when the
 * new activation byte equals the old one, so a second Activate while active
 * starts no script and raises no event.
 */
namespace rwe
{
    namespace
    {
        std::shared_ptr<CobScript> makeActivationScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            script->functions.push_back(CobFunctionInfo{"Activate", 0});
            script->functions.push_back(CobFunctionInfo{"Deactivate", 1});
            script->instructions = std::vector<uint32_t>{
                static_cast<uint32_t>(OpCode::RETURN),
                static_cast<uint32_t>(OpCode::RETURN),
            };
            return script;
        }

        int countEvents(const GameSimulation& sim, bool activated)
        {
            int n = 0;
            for (const auto& e : sim.events)
            {
                if (activated ? std::holds_alternative<UnitActivatedEvent>(e) : std::holds_alternative<UnitDeactivatedEvent>(e))
                {
                    ++n;
                }
            }
            return n;
        }
    }

    TEST_CASE("Activate and Deactivate run their scripts and raise their events only on a change", "[activation]")
    {
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayer(sim);

        std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        UnitDefinition d{};
        d.objectName = "model";
        d.maxHitPoints = 100;
        d.buildTime = 0u;
        d.onOffable = true;
        d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["radar"] = d;

        auto script = makeActivationScript();
        auto unitId = addUnitOfType(sim, "radar", player, SimVector(16_ss, 0_ss, 16_ss), script);
        auto& env = *sim.getUnitState(unitId).cobEnvironment;
        REQUIRE_FALSE(sim.getUnitState(unitId).activated);

        SECTION("two consecutive activates start the script once")
        {
            sim.activateUnit(unitId);
            REQUIRE(env.threads.size() == 1);
            REQUIRE(env.threads.front()->name == "Activate");
            REQUIRE(countEvents(sim, true) == 1);

            sim.activateUnit(unitId);
            REQUIRE(env.threads.size() == 1);
            REQUIRE(countEvents(sim, true) == 1);
            REQUIRE(sim.getUnitState(unitId).activated);
        }

        SECTION("a deactivate of a unit that is already off does nothing")
        {
            sim.deactivateUnit(unitId);
            REQUIRE(env.threads.empty());
            REQUIRE(countEvents(sim, false) == 0);
        }

        SECTION("a real change in each direction runs its script and raises its event")
        {
            sim.activateUnit(unitId);
            sim.deactivateUnit(unitId);
            sim.deactivateUnit(unitId);
            REQUIRE(env.threads.size() == 2);
            REQUIRE(env.threads.back()->name == "Deactivate");
            REQUIRE(countEvents(sim, true) == 1);
            REQUIRE(countEvents(sim, false) == 1);
            REQUIRE_FALSE(sim.getUnitState(unitId).activated);
        }
    }
}
