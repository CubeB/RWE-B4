#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitModelVertices.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <vector>

/**
 * BT-60, docs/BYTE-TACTICS-SPEC-ROOM-FINDINGS.md on branch spec-room-findings
 * (static byte-tactics reading at 97d7ac59, not executed against the
 * original): the aim point at a unit target is the midpoint of the bounding
 * box of the SweetSpot piece's posed vertices, with both bounds of each axis
 * starting at zero, plus the unit's position. The piece defaults to piece 0.
 * The routine does not negate z, so an off-centre z extent is mirrored against
 * where the piece is drawn (medium confidence in the finding).
 */
namespace rwe
{
    namespace
    {
        /** A script whose SweetSpot returns the given piece number, or has no SweetSpot at all. */
        std::shared_ptr<CobScript> makeScript(std::optional<unsigned int> sweetSpotPiece)
        {
            auto script = makeEmptyCobScript({"base", "turret"});
            if (sweetSpotPiece)
            {
                script->instructions = {
                    static_cast<uint32_t>(OpCode::PUSH_CONSTANT),
                    *sweetSpotPiece,
                    static_cast<uint32_t>(OpCode::POP_LOCAL_VAR),
                    0u,
                    static_cast<uint32_t>(OpCode::PUSH_CONSTANT),
                    0u,
                    static_cast<uint32_t>(OpCode::RETURN)};
                script->functions.push_back(CobFunctionInfo{"SweetSpot", 0u});
            }
            return script;
        }

        UnitDefinition makeDef(const std::string& model)
        {
            UnitDefinition d{};
            d.objectName = model;
            d.isMobile = true;
            d.canMove = true;
            d.maxHitPoints = 250;
            d.buildTime = 0u;
            d.sightDistance = 280u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 17u, 255u, 0u, 12u};
            return d;
        }

        /**
         * Two pieces: base at the origin, turret ten up. Base vertices span
         * x 10, y 0 to 4, z 0 to 6 (so every box includes zero on x and the
         * lower bounds); the turret's, in its own space, sit at x 0, y 2 to 6,
         * z -8 to -4, which the ten of offset lifts to y 12 to 16.
         */
        void registerModel(GameSimulation& sim, const std::string& name, bool withVertices)
        {
            std::vector<UnitPieceDefinition> pieces{
                UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt},
                UnitPieceDefinition{"turret", SimVector(0_ss, 10_ss, 0_ss), std::string("base")}};
            auto model = createUnitModelDefinition(16_ss, std::move(pieces));
            if (withVertices)
            {
                auto table = std::make_shared<UnitModelVertices>();
                table->perPiece.push_back({SimVector(10_ss, 0_ss, 0_ss), SimVector(10_ss, 4_ss, 6_ss)});
                table->perPiece.push_back({SimVector(0_ss, 2_ss, -8_ss), SimVector(0_ss, 6_ss, -4_ss)});
                model.vertices = table;
            }
            sim.unitModelDefinitions[name] = std::move(model);
            sim.unitDefinitions[name] = makeDef(name);
        }

        UnitId spawn(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos, SimAngle rotation, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            UnitMesh turret;
            turret.name = "turret";
            std::vector<UnitMesh> pieces{base, turret};
            UnitState unit(pieces, std::move(env));
            unit.unitType = type;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.rotation = rotation;
            unit.hitPoints = 250;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        bool near(SimScalar a, float b)
        {
            return a.value == Catch::Approx(b).margin(0.01);
        }
    }

    TEST_CASE("a unit's aim point is the midpoint of its default piece's vertex box with zero inside it", "[sweetspot]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        registerModel(sim, "tank", true);
        auto player = addPlayer(sim);
        auto script = makeScript(std::nullopt);
        auto id = spawn(sim, "tank", player, SimVector(100_ss, 20_ss, 50_ss), SimAngle(0), script);

        auto aim = sim.getUnitState(id).position;
        UnitBehaviorService service(&sim);
        aim = service.getSweetSpot(id);

        // x 0..10 (zero is in the box, so 5 and not 10), y 0..4 -> 2, and z 0..6
        // -> 3 which the routine adds un-negated: a mirror of where the box is
        // drawn, and an offset of -3 in RWE's frame.
        REQUIRE(near(aim.x, 105.0f));
        REQUIRE(near(aim.y, 22.0f));
        REQUIRE(near(aim.z, 47.0f));
    }

    TEST_CASE("the SweetSpot piece's box includes the piece offset and the mirrored z", "[sweetspot]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        registerModel(sim, "tank", true);
        auto player = addPlayer(sim);
        auto script = makeScript(1u);
        auto id = spawn(sim, "tank", player, SimVector(100_ss, 20_ss, 50_ss), SimAngle(0), script);

        UnitBehaviorService service(&sim);
        auto aim = service.getSweetSpot(id);

        // Posed turret vertices: x 0, y 12..16 (the ten of offset included, zero
        // still in the box so y 0..16 -> 8), z -8..-4 (box -8..0 -> -4).
        // The original adds that z as it stands, which in RWE's flipped frame is +4.
        REQUIRE(near(aim.x, 100.0f));
        REQUIRE(near(aim.y, 28.0f));
        REQUIRE(near(aim.z, 54.0f));
    }

    TEST_CASE("the vertex box is posed by the unit's heading", "[sweetspot]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        registerModel(sim, "tank", true);
        auto player = addPlayer(sim);
        auto script = makeScript(std::nullopt);
        auto id = spawn(sim, "tank", player, SimVector(100_ss, 20_ss, 50_ss), SimAngle(32768), script);

        UnitBehaviorService service(&sim);
        auto aim = service.getSweetSpot(id);

        // Half a turn takes x 0..10 to -10..0 and z 0..6 to -6..0.
        REQUIRE(near(aim.x, 95.0f));
        REQUIRE(near(aim.y, 22.0f));
        REQUIRE(near(aim.z, 53.0f));
    }

    TEST_CASE("a model with no vertices, or a piece the script does not have, aims at the unit's position", "[sweetspot]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        registerModel(sim, "bare", false);
        registerModel(sim, "tank", true);
        auto player = addPlayer(sim);
        auto bare = spawn(sim, "bare", player, SimVector(100_ss, 20_ss, 50_ss), SimAngle(0), makeScript(std::nullopt));
        auto beyond = spawn(sim, "tank", player, SimVector(300_ss, 20_ss, 50_ss), SimAngle(0), makeScript(7u));

        UnitBehaviorService service(&sim);
        auto aimBare = service.getSweetSpot(bare);
        REQUIRE(near(aimBare.x, 100.0f));
        REQUIRE(near(aimBare.y, 20.0f));
        REQUIRE(near(aimBare.z, 50.0f));
        auto aimBeyond = service.getSweetSpot(beyond);
        REQUIRE(near(aimBeyond.x, 300.0f));
        REQUIRE(near(aimBeyond.y, 20.0f));
        REQUIRE(near(aimBeyond.z, 50.0f));
    }
}
