#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <random>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/RepairPatrol.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitMovementOrders.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>

/**
 * A constructor on patrol, TOTALA-EXE-ECONOMY.md S:131: RepairPatrol
 * (0x405980 on the ground, VTOL_RepairPatrol 0x4152F0 in the air) repairs
 * what is hurt in sight, helps build what is unfinished, and reclaims what is
 * lying about -- on a 60 or 45 tick timer, one random pick for the repair,
 * the best of three draws for each reclaim list.
 */
namespace rwe
{
    namespace
    {
        constexpr float MetalCapacity = 1050.0f;
        constexpr float EnergyCapacity = 3050.0f;

        UnitDefinition makeBuilderDef(bool flies)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canFly = flies;
            d.builder = true;
            d.canReclamate = true;
            d.workerTimePerTick = 2u;
            d.maxHitPoints = 700;
            d.sightDistance = 235u;
            d.buildDistance = 40_ss;
            d.buildTime = 0u;
            d.cruiseAltitude = 100_ss;
            d.maxVelocity = 0.8_ssf;
            d.acceleration = 0.12_ssf;
            d.brakeRate = 0.24_ssf;
            d.turnRate = 1020_ss;
            d.maneuverLeashLength = 500_ss;
            d.energyStorage = Energy(50.0f);
            d.metalStorage = Metal(50.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitDefinition makePatientDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = false;
            d.canMove = false;
            d.maxHitPoints = 700;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitDefinition makeStorageDef(float metalStorage, float energyStorage)
        {
            auto d = makePatientDef();
            d.metalStorage = Metal(metalStorage);
            d.energyStorage = Energy(energyStorage);
            return d;
        }

        FeatureDefinition makeFeatureDef(const std::string& name, unsigned int metal, unsigned int energy)
        {
            FeatureDefinition d{};
            d.name = name;
            d.footprintX = 1;
            d.footprintZ = 1;
            d.height = 10_ss;
            d.reclaimable = true;
            d.autoreclaimable = true;
            d.metal = metal;
            d.energy = energy;
            d.damage = 10;
            d.blocking = false;
            return d;
        }

        PlayerId addHoldingPlayer(GameSimulation& sim, float metal, float energy, std::optional<int> team = std::nullopt)
        {
            GamePlayerInfo p{
                std::optional<std::string>("builder"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(metal),
                Energy(energy),
                Metal(MetalCapacity),
                Energy(EnergyCapacity),
                Metal(MetalCapacity),
                Energy(EnergyCapacity),
                team};
            return sim.addPlayer(p);
        }

        void addStorage(GameSimulation& sim, PlayerId owner, const std::shared_ptr<CobScript>& script)
        {
            sim.unitDefinitions["armmstor"] = makeStorageDef(1000.0f, 0.0f);
            sim.unitDefinitions["armestor"] = makeStorageDef(0.0f, 3000.0f);
            addUnitOfType(sim, "armmstor", owner, SimVector(1500_ss, 0_ss, 1500_ss), script);
            addUnitOfType(sim, "armestor", owner, SimVector(1560_ss, 0_ss, 1500_ss), script);
        }

        struct Field
        {
            std::shared_ptr<CobScript> script{makeEmptyCobScript()};
            GameSimulation sim;
            PlayerId us;

            explicit Field(float metal, float energy)
                : sim(makeFlatTerrain(128, 128), 0u, 0, 0),
                  us(addHoldingPlayer(sim, metal, energy))
            {
                addStorage(sim, us, script);
                sim.unitDefinitions["builder"] = makeBuilderDef(false);
                sim.unitDefinitions["flyer"] = makeBuilderDef(true);
                sim.unitDefinitions["patient"] = makePatientDef();
            }

            UnitId addBuilder(const SimVector& position, bool flies = false)
            {
                auto id = addUnitOfType(sim, flies ? "flyer" : "builder", us, position, script);
                auto& unit = sim.getUnitState(id);
                unit.hitPoints = 700;
                if (flies)
                {
                    unit.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
                    sim.flyingUnitsSet.insert(id);
                }
                return id;
            }

            UnitId addHurtPatient(const SimVector& position, PlayerId owner)
            {
                auto id = addUnitOfType(sim, "patient", owner, position, script);
                sim.getUnitState(id).hitPoints = 100;
                return id;
            }

            FeatureId addFeatureNear(FeatureDefinitionId def, const SimVector& position)
            {
                auto cell = sim.terrain.worldToHeightmapCoordinate(position);
                return sim.addFeature(def, cell.x, cell.y).value();
            }
        };

        ConstUnitInfo infoOf(const GameSimulation& sim, UnitId id)
        {
            const auto& state = sim.getUnitState(id);
            return ConstUnitInfo(id, &state, &sim.unitDefinitions.at(state.unitType));
        }
    }

    TEST_CASE("repair patrol: the cadence is 60 ticks on the ground and 45 in the air", "[repairpatrol]")
    {
        REQUIRE(repairPatrolPeriod(false) == 60u);
        REQUIRE(repairPatrolPeriod(true) == 45u);
    }

    TEST_CASE("repair patrol: the repair pick takes own and allied units that are hurt, in sight, on the flat", "[repairpatrol]")
    {
        Field f(MetalCapacity, EnergyCapacity);
        auto them = addHoldingPlayer(f.sim, 0.0f, 0.0f);
        auto ally = addHoldingPlayer(f.sim, 0.0f, 0.0f, 7);
        f.sim.getPlayer(f.us).teamId = 7;

        auto builder = f.addBuilder(SimVector(500_ss, 0_ss, 500_ss));

        SECTION("nothing hurt, nothing picked")
        {
            auto whole = addUnitOfType(f.sim, "patient", f.us, SimVector(560_ss, 0_ss, 500_ss), f.script);
            f.sim.getUnitState(whole).hitPoints = 700;
            REQUIRE_FALSE(pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder)).has_value());
        }

        SECTION("own and allied, but not an enemy")
        {
            f.addHurtPatient(SimVector(560_ss, 0_ss, 500_ss), them);
            REQUIRE_FALSE(pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder)).has_value());

            auto allied = f.addHurtPatient(SimVector(560_ss, 0_ss, 520_ss), ally);
            auto picked = pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder));
            REQUIRE(picked.has_value());
            REQUIRE(*picked == allied);
        }

        SECTION("sight is a flat radius: 235 is in, 236 is out, and height does not count")
        {
            auto edge = f.addHurtPatient(SimVector(735_ss, 400_ss, 500_ss), f.us);
            REQUIRE(pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder)).has_value());
            f.sim.getUnitState(edge).position = SimVector(736_ss, 0_ss, 500_ss);
            REQUIRE_FALSE(pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder)).has_value());
        }

        SECTION("a unit being reclaimed by the player is refused")
        {
            auto victim = f.addHurtPatient(SimVector(560_ss, 0_ss, 500_ss), f.us);
            auto other = f.addBuilder(SimVector(900_ss, 0_ss, 900_ss));
            f.sim.getUnitState(other).behaviourState = UnitBehaviorStateReclaiming{victim, std::nullopt, 0};
            REQUIRE_FALSE(pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder)).has_value());
        }

        SECTION("an unfinished frame counts as hurt")
        {
            f.sim.unitDefinitions["frame"] = makePatientDef();
            f.sim.unitDefinitions["frame"].buildTime = 1000u;
            auto frame = addUnitOfType(f.sim, "frame", f.us, SimVector(560_ss, 0_ss, 500_ss), f.script);
            f.sim.getUnitState(frame).buildTimeCompleted = 0u;
            f.sim.getUnitState(frame).hitPoints = 700;
            auto picked = pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder));
            REQUIRE(picked.has_value());
            REQUIRE(*picked == frame);
        }
    }

    TEST_CASE("repair patrol: repair needs a fifth of the energy cap", "[repairpatrol]")
    {
        Field f(MetalCapacity, 0.0f);
        auto builder = f.addBuilder(SimVector(500_ss, 0_ss, 500_ss));
        f.addHurtPatient(SimVector(560_ss, 0_ss, 500_ss), f.us);

        // 0.2 * 3050 = 610.
        f.sim.getPlayer(f.us).energy = Energy(609.0f);
        REQUIRE_FALSE(pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder)).has_value());

        f.sim.getPlayer(f.us).energy = Energy(610.0f);
        REQUIRE(pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder)).has_value());
    }

    TEST_CASE("repair patrol: one candidate costs no draw, several cost one", "[repairpatrol]")
    {
        Field f(MetalCapacity, EnergyCapacity);
        auto builder = f.addBuilder(SimVector(500_ss, 0_ss, 500_ss));
        auto first = f.addHurtPatient(SimVector(560_ss, 0_ss, 500_ss), f.us);

        auto before = f.sim.rng;
        REQUIRE(*pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder)) == first);
        REQUIRE(f.sim.rng == before);

        auto second = f.addHurtPatient(SimVector(560_ss, 0_ss, 520_ss), f.us);
        std::minstd_rand expected = f.sim.rng;
        auto index = expected() % 2u;

        // Cell order: the same 128-unit cell, so unit id breaks the tie.
        auto picked = *pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder));
        REQUIRE(f.sim.rng == expected);
        REQUIRE(picked == (index == 0 ? first : second));
    }

    TEST_CASE("repair patrol: the reclaim scan is a grid, 48 units apart, over half the side", "[repairpatrol]")
    {
        // A side of 240 gives a half-extent of 120 and six samples a row,
        // at -120, -72, -24, 24, 72, 120 about the unit.
        Field f(0.0f, 0.0f);
        auto plantDefinition = f.sim.featureDefinitions.insert(makeFeatureDef("plant", 0, 50));
        auto position = SimVector(800_ss, 0_ss, 800_ss);
        auto builder = f.addBuilder(position, true);

        SECTION("a feature on a sample is found")
        {
            auto id = f.addFeatureNear(plantDefinition, SimVector(800_ss + 72_ss, 0_ss, 800_ss - 24_ss));
            auto picked = pickRepairPatrolReclaimTarget(f.sim, infoOf(f.sim, builder), 240_ss);
            REQUIRE(picked.has_value());
            REQUIRE(*picked == id);
        }

        SECTION("one between samples is not")
        {
            f.addFeatureNear(plantDefinition, SimVector(800_ss + 48_ss, 0_ss, 800_ss));
            REQUIRE_FALSE(pickRepairPatrolReclaimTarget(f.sim, infoOf(f.sim, builder), 240_ss).has_value());
        }

        SECTION("and one beyond the half-extent is not")
        {
            f.addFeatureNear(plantDefinition, SimVector(800_ss + 168_ss, 0_ss, 800_ss));
            REQUIRE_FALSE(pickRepairPatrolReclaimTarget(f.sim, infoOf(f.sim, builder), 240_ss).has_value());
        }
    }

    TEST_CASE("repair patrol: the best of three draws is taken from each list, and the ladder chooses", "[repairpatrol]")
    {
        Field f(MetalCapacity, EnergyCapacity);
        auto small = f.sim.featureDefinitions.insert(makeFeatureDef("small", 10, 0));
        auto big = f.sim.featureDefinitions.insert(makeFeatureDef("big", 100, 0));
        auto power = f.sim.featureDefinitions.insert(makeFeatureDef("power", 0, 200));
        auto position = SimVector(800_ss, 0_ss, 800_ss);
        auto builder = f.addBuilder(position, true);

        // Two metal features and two energy ones, all on samples (-24 and 24
        // either way of the unit).
        auto a = f.addFeatureNear(small, SimVector(800_ss - 24_ss, 0_ss, 800_ss - 24_ss));
        auto b = f.addFeatureNear(big, SimVector(800_ss + 24_ss, 0_ss, 800_ss - 24_ss));
        auto c = f.addFeatureNear(power, SimVector(800_ss - 24_ss, 0_ss, 800_ss + 24_ss));
        auto d = f.addFeatureNear(power, SimVector(800_ss + 24_ss, 0_ss, 800_ss + 24_ss));
        (void)a;
        (void)b;
        (void)c;
        (void)d;

        SECTION("six draws in all: three for each list of two")
        {
            std::minstd_rand expected = f.sim.rng;
            for (int i = 0; i < 6; ++i)
            {
                expected();
            }

            f.sim.getPlayer(f.us).metal = Metal(0.0f);
            auto picked = pickRepairPatrolReclaimTarget(f.sim, infoOf(f.sim, builder), 240_ss);
            REQUIRE(picked.has_value());
            REQUIRE(f.sim.rng == expected);
        }

        SECTION("short of metal, the metal list's best is taken, and the best of three is the biggest it drew")
        {
            f.sim.getPlayer(f.us).metal = Metal(0.0f);

            // With three draws from two entries the small one wins only if
            // all three come up on it. Work that out from the generator.
            std::minstd_rand probe = f.sim.rng;
            bool allSmall = true;
            for (int i = 0; i < 3; ++i)
            {
                // The metal list is in sample order, so the small one is index 0.
                if ((probe() % 2u) != 0u)
                {
                    allSmall = false;
                }
            }

            auto picked = pickRepairPatrolReclaimTarget(f.sim, infoOf(f.sim, builder), 240_ss);
            REQUIRE(picked.has_value());
            REQUIRE(*picked == (allSmall ? a : b));
        }

        SECTION("full of metal and short of energy, the energy goes")
        {
            f.sim.getPlayer(f.us).metal = Metal(MetalCapacity);
            f.sim.getPlayer(f.us).energy = Energy(0.0f);
            auto picked = pickRepairPatrolReclaimTarget(f.sim, infoOf(f.sim, builder), 240_ss);
            REQUIRE(picked.has_value());
            REQUIRE((*picked == c || *picked == d));
        }

        SECTION("full of everything, nothing fits, and nothing is picked")
        {
            f.sim.getPlayer(f.us).metal = Metal(MetalCapacity);
            f.sim.getPlayer(f.us).energy = Energy(EnergyCapacity);
            REQUIRE_FALSE(pickRepairPatrolReclaimTarget(f.sim, infoOf(f.sim, builder), 240_ss).has_value());
        }
    }

    namespace
    {
        /** Ticks until the front order is not the patrol, or the budget is spent. */
        int tickUntilJob(GameSimulation& sim, UnitId id, int budget)
        {
            for (int i = 1; i <= budget; ++i)
            {
                sim.tick();
                const auto& orders = sim.getUnitState(id).orders;
                if (!orders.empty() && !std::holds_alternative<PatrolOrder>(orders.front()))
                {
                    return i;
                }
            }
            return -1;
        }
    }

    TEST_CASE("repair patrol: a ground constructor repairs on its 60 tick timer, with a leash and a way home", "[repairpatrol]")
    {
        Field f(MetalCapacity, EnergyCapacity);
        auto position = SimVector(500_ss, 0_ss, 500_ss);
        auto builder = f.addBuilder(position);
        f.sim.getUnitState(builder).moveOrders = UnitMovementOrders::HoldPosition;
        auto patient = f.addHurtPatient(SimVector(600_ss, 0_ss, 500_ss), f.us);
        f.sim.getUnitState(builder).inBuildStance = true;
        f.sim.getUnitState(builder).orders.push_back(PatrolOrder(position));
        f.sim.getUnitState(builder).orders.push_back(PatrolOrder(SimVector(500_ss, 0_ss, 900_ss)));

        auto start = f.sim.gameTime.value;
        auto took = tickUntilJob(f.sim, builder, 200);
        REQUIRE(took > 0);

        // It happened on the unit's own phase of the 60 tick period and
        // nowhere else.
        REQUIRE(((start + static_cast<unsigned int>(took) + builder.value) % 60u) == 0u);

        const auto& orders = f.sim.getUnitState(builder).orders;
        REQUIRE(orders.size() == 4);

        // [job][move home][...]
        const auto* repair = std::get_if<RepairOrder>(&orders[0]);
        REQUIRE(repair != nullptr);
        REQUIRE(repair->target == patient);

        // Hold Position or Maneuver: a leash of the unit's sight for the one the
        // fixture starts on, or maneuverleashlength for the other.
        REQUIRE(repair->leash.has_value());

        const auto* home = std::get_if<MoveOrder>(&orders[1]);
        REQUIRE(home != nullptr);
    }

    TEST_CASE("repair patrol: the leash follows the move-order mode, and Roam has neither leash nor way home", "[repairpatrol]")
    {
        auto setup = [](UnitMovementOrders mode, std::vector<UnitOrder>& out, std::optional<AttackLeash>& leash) {
            Field f(MetalCapacity, EnergyCapacity);
            auto position = SimVector(500_ss, 0_ss, 500_ss);
            auto builder = f.addBuilder(position);
            f.addHurtPatient(SimVector(600_ss, 0_ss, 500_ss), f.us);
            f.sim.getUnitState(builder).moveOrders = mode;
            f.sim.getUnitState(builder).orders.push_back(PatrolOrder(position));
            REQUIRE(tickUntilJob(f.sim, builder, 200) > 0);
            const auto& orders = f.sim.getUnitState(builder).orders;
            out.assign(orders.begin(), orders.end());
            if (auto repair = std::get_if<RepairOrder>(&orders.front()))
            {
                leash = repair->leash;
            }
        };

        std::vector<UnitOrder> orders;
        std::optional<AttackLeash> leash;

        setup(UnitMovementOrders::HoldPosition, orders, leash);
        REQUIRE(leash.has_value());
        REQUIRE(leash->distance == 235_ss);
        REQUIRE(std::holds_alternative<MoveOrder>(orders.at(1)));

        orders.clear();
        leash.reset();
        setup(UnitMovementOrders::Maneuver, orders, leash);
        REQUIRE(leash.has_value());
        REQUIRE(leash->distance == 500_ss);
        REQUIRE(std::holds_alternative<MoveOrder>(orders.at(1)));

        orders.clear();
        leash.reset();
        setup(UnitMovementOrders::Roam, orders, leash);
        REQUIRE_FALSE(leash.has_value());
        REQUIRE(std::holds_alternative<PatrolOrder>(orders.at(1)));
    }

    TEST_CASE("repair patrol: an aircraft helps build with no leash and no way home, on a 45 tick timer", "[repairpatrol]")
    {
        Field f(MetalCapacity, EnergyCapacity);
        f.sim.unitDefinitions["frame"] = makePatientDef();
        f.sim.unitDefinitions["frame"].buildTime = 1000u;

        auto position = SimVector(500_ss, 100_ss, 500_ss);
        auto builder = f.addBuilder(position, true);
        auto frame = addUnitOfType(f.sim, "frame", f.us, SimVector(560_ss, 0_ss, 500_ss), f.script);
        f.sim.getUnitState(frame).buildTimeCompleted = 0u;
        f.sim.getUnitState(frame).hitPoints = 700;

        f.sim.getUnitState(builder).orders.push_back(PatrolOrder(SimVector(500_ss, 0_ss, 520_ss)));

        auto start = f.sim.gameTime.value;
        auto took = tickUntilJob(f.sim, builder, 200);
        REQUIRE(took > 0);
        REQUIRE(((start + static_cast<unsigned int>(took) + builder.value) % 45u) == 0u);

        const auto& orders = f.sim.getUnitState(builder).orders;
        const auto* repair = std::get_if<RepairOrder>(&orders.front());
        REQUIRE(repair != nullptr);
        REQUIRE(repair->target == frame);
        REQUIRE_FALSE(repair->leash.has_value());
        REQUIRE(orders.size() == 2);
        REQUIRE(std::holds_alternative<PatrolOrder>(orders.back()));
    }

    TEST_CASE("repair patrol: a repair job given up at its leash", "[repairpatrol]")
    {
        Field f(MetalCapacity, EnergyCapacity);
        auto builder = f.addBuilder(SimVector(500_ss, 0_ss, 500_ss));
        auto patient = f.addHurtPatient(SimVector(520_ss, 0_ss, 500_ss), f.us);

        auto& orders = f.sim.getUnitState(builder).orders;
        orders.push_back(RepairOrder(patient, AttackLeash(SimVector(0_ss, 0_ss, 0_ss), 100_ss)));
        f.sim.tick();

        // The anchor is 700 away from where it stands: the job is dropped.
        REQUIRE(orders.empty());
    }

    TEST_CASE("repair patrol: a ground constructor does not look for wreckage with the stores full", "[repairpatrol]")
    {
        Field f(MetalCapacity, EnergyCapacity);
        auto plantDefinition = f.sim.featureDefinitions.insert(makeFeatureDef("plant", 0, 50));
        auto position = SimVector(800_ss, 0_ss, 800_ss);
        auto builder = f.addBuilder(position);
        f.addFeatureNear(plantDefinition, SimVector(800_ss + 24_ss, 0_ss, 800_ss + 24_ss));
        f.sim.getUnitState(builder).orders.push_back(PatrolOrder(position));

        for (int i = 0; i < 130; ++i)
        {
            f.sim.tick();
            const auto& orders = f.sim.getUnitState(builder).orders;
            REQUIRE_FALSE(std::holds_alternative<ReclaimOrder>(orders.front()));
        }
    }

    TEST_CASE("repair patrol: an aircraft reclaims whatever the stores", "[repairpatrol]")
    {
        // The air routine runs the reclaim stage unconditionally; the ladder
        // still needs room for the payout.
        Field f(0.0f, 0.0f);
        auto metalDefinition = f.sim.featureDefinitions.insert(makeFeatureDef("wreck", 40, 0));
        auto position = SimVector(800_ss, 100_ss, 800_ss);
        auto builder = f.addBuilder(position, true);
        auto wreck = f.addFeatureNear(metalDefinition, SimVector(800_ss + 24_ss, 0_ss, 800_ss + 24_ss));
        f.sim.getUnitState(builder).orders.push_back(PatrolOrder(position));

        auto took = tickUntilJob(f.sim, builder, 200);
        REQUIRE(took > 0);
        const auto* reclaim = std::get_if<ReclaimOrder>(&f.sim.getUnitState(builder).orders.front());
        REQUIRE(reclaim != nullptr);
        REQUIRE(std::get<FeatureId>(reclaim->target) == wreck);
    }

    TEST_CASE("community patch: the energy level gates assisting at or above it", "[repairpatrol][communitypatch]")
    {
        // TADR 2026.10.6: "a patrolling unit assists only while energy is at
        // or above it". At 0 the gate can never fail, so it always assists;
        // at 100 only a full store assists.
        Field f(MetalCapacity, EnergyCapacity);
        f.sim.communityPatchMode = true;
        auto builder = f.addBuilder(SimVector(500_ss, 0_ss, 500_ss));
        f.addHurtPatient(SimVector(560_ss, 0_ss, 500_ss), f.us);

        auto& player = f.sim.getPlayer(f.us);

        SECTION("80% needs four fifths of the energy cap")
        {
            player.reclaimEnergyPercent = 80;
            player.energy = Energy(0.79f * EnergyCapacity);
            REQUIRE_FALSE(pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder)).has_value());

            player.energy = Energy(0.8f * EnergyCapacity);
            REQUIRE(pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder)).has_value());
        }

        SECTION("0 always assists, even with nothing in the store")
        {
            player.reclaimEnergyPercent = 0;
            player.energy = Energy(0.0f);
            REQUIRE(pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder)).has_value());
        }

        SECTION("100 assists only when the energy store is exactly full")
        {
            player.reclaimEnergyPercent = 100;
            player.energy = Energy(EnergyCapacity - 1.0f);
            REQUIRE_FALSE(pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder)).has_value());

            player.energy = Energy(EnergyCapacity);
            REQUIRE(pickRepairPatrolRepairTarget(f.sim, infoOf(f.sim, builder)).has_value());
        }
    }

    TEST_CASE("community patch: a ground constructor's reclaim levels gate the whole scan", "[repairpatrol][communitypatch]")
    {
        // "A patrolling construction unit looks for something to reclaim only
        // while metal or energy is below [its level]". The ladder's two
        // "still fits" rungs therefore cannot reclaim on their own: at 0 the
        // gate never passes and the patrol does not reclaim at all.
        auto run = [](bool mode, int metalLevel, int energyLevel) {
            Field f(0.0f, 0.0f);
            f.sim.communityPatchMode = mode;
            f.sim.getPlayer(f.us).reclaimMetalPercent = metalLevel;
            f.sim.getPlayer(f.us).reclaimEnergyPercent = energyLevel;
            auto metalDefinition = f.sim.featureDefinitions.insert(makeFeatureDef("wreck", 40, 0));
            auto position = SimVector(800_ss, 0_ss, 800_ss);
            auto builder = f.addBuilder(position);
            f.addFeatureNear(metalDefinition, SimVector(800_ss + 24_ss, 0_ss, 800_ss + 24_ss));
            f.sim.getUnitState(builder).orders.push_back(PatrolOrder(position));
            return tickUntilJob(f.sim, builder, 70);
        };

        // 0 never counts as low, so no reclaim -- where the mode-off game,
        // whose fixed fifth is above an empty store, does reclaim.
        REQUIRE(run(true, 0, 0) == -1);
        REQUIRE(run(false, 0, 0) > 0);
    }

    TEST_CASE("community patch: 100 counts as low until the store is exactly full", "[repairpatrol][communitypatch]")
    {
        auto run = [](float metal, float energy) {
            Field f(metal, energy);
            f.sim.communityPatchMode = true;
            f.sim.getPlayer(f.us).reclaimMetalPercent = 100;
            f.sim.getPlayer(f.us).reclaimEnergyPercent = 100;
            auto metalDefinition = f.sim.featureDefinitions.insert(makeFeatureDef("wreck", 40, 0));
            auto position = SimVector(800_ss, 0_ss, 800_ss);
            auto builder = f.addBuilder(position);
            f.addFeatureNear(metalDefinition, SimVector(800_ss + 24_ss, 0_ss, 800_ss + 24_ss));
            f.sim.getUnitState(builder).orders.push_back(PatrolOrder(position));
            return tickUntilJob(f.sim, builder, 70);
        };

        REQUIRE(run(MetalCapacity, EnergyCapacity) == -1);
        REQUIRE(run(MetalCapacity - 1.0f, EnergyCapacity) > 0);
    }

    TEST_CASE("community patch: a reclaim level is a percentage of the storage, not a fixed amount", "[repairpatrol][communitypatch]")
    {
        // The same 600 metal is above half of a 1050 store and below half of a
        // larger one, so the level follows the cap as it grows.
        Field f(600.0f, EnergyCapacity);
        f.sim.communityPatchMode = true;
        auto& player = f.sim.getPlayer(f.us);
        player.reclaimMetalPercent = 50;
        player.reclaimEnergyPercent = 0;

        auto huge = f.sim.featureDefinitions.insert(makeFeatureDef("huge", 2000, 0));
        auto position = SimVector(800_ss, 0_ss, 800_ss);
        auto builder = f.addBuilder(position, true);
        f.addFeatureNear(huge, SimVector(800_ss + 24_ss, 0_ss, 800_ss + 24_ss));

        player.maxMetal = Metal(1050.0f);
        REQUIRE_FALSE(pickRepairPatrolReclaimTarget(f.sim, infoOf(f.sim, builder), 240_ss).has_value());

        player.maxMetal = Metal(3000.0f);
        REQUIRE(pickRepairPatrolReclaimTarget(f.sim, infoOf(f.sim, builder), 240_ss).has_value());
    }

    TEST_CASE("community patch: an air constructor follows the same reclaim gate as a ground one", "[repairpatrol][communitypatch]")
    {
        auto run = [](bool mode, float metal) {
            Field f(metal, EnergyCapacity);
            f.sim.communityPatchMode = mode;
            auto metalDefinition = f.sim.featureDefinitions.insert(makeFeatureDef("wreck", 40, 0));
            auto position = SimVector(800_ss, 100_ss, 800_ss);
            auto builder = f.addBuilder(position, true);
            f.addFeatureNear(metalDefinition, SimVector(800_ss + 24_ss, 0_ss, 800_ss + 24_ss));
            f.sim.getUnitState(builder).orders.push_back(PatrolOrder(position));
            return tickUntilJob(f.sim, builder, 70);
        };

        // Above the default fifth: the original's air routine reclaimed any
        // stock it could store; the community rule does not look at all.
        REQUIRE(run(false, 0.5f * MetalCapacity) > 0);
        REQUIRE(run(true, 0.5f * MetalCapacity) == -1);

        // Below it, the air constructor still reclaims.
        REQUIRE(run(true, 0.1f * MetalCapacity) > 0);
    }
}
