#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitSpatialIndex.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        /**
         * A generator for the test's own layouts. Deliberately not the
         * simulation's: drawing from that would move the very stream the
         * thing under test is being judged on.
         */
        struct TestRandom
        {
            unsigned int state;

            explicit TestRandom(unsigned int seed) : state(seed) {}

            unsigned int next()
            {
                state = (state * 1103515245u) + 12345u;
                return (state >> 16u) & 0x7FFFu;
            }

            /** A whole number in [lo, hi]. */
            int between(int lo, int hi)
            {
                return lo + static_cast<int>(next() % static_cast<unsigned int>(hi - lo + 1));
            }
        };

        MapTerrain makeBattleTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addBattlePlayer(GameSimulation& sim, const std::string& name)
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

        std::shared_ptr<CobScript> makeBattleScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        /**
         * Gun emplacements: they shoot, they do not move, and they see the
         * whole of the test map, so every unit on it is a live candidate for
         * every other unit's search.
         */
        void defineBattleUnits(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.canAttack = true;
            d.sightDistance = 2000u;
            d.maxHitPoints = 40;
            d.buildTime = 0u;
            d.shootMe = true;
            d.category = "ARM LEVEL1 WEAPON NOTAIR NOTSUB";
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions["gun"] = d;

            WeaponDefinition w{};
            w.maxRange = 400_ss;
            w.reloadTime = 15_ss;
            w.burst = 1;
            w.velocity = 450_ss / 30_ss;
            w.damageRadius = 4_ss;
            w.damage["DEFAULT"] = 10;
            sim.weaponDefinitions["gun"] = w;
        }

        UnitId spawnBattleUnit(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = "gun";
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at("gun").maxHitPoints;
            unit.fireOrders = UnitFireOrders::FireAtWill;

            auto id = sim.tryAddUnit(std::move(unit)).value();

            UnitWeapon weapon;
            weapon.weaponType = "gun";
            sim.getUnitState(id).weapons[0] = weapon;
            return id;
        }

        /**
         * Two sides facing one another on a lattice, near enough that plenty
         * is in range of plenty. Spacing is a comfortable multiple of the
         * heightmap cell so no two units want the same footprint; the row
         * jitter is there so the units do not line up with the index's cell
         * boundaries.
         */
        void layOutBattle(GameSimulation& sim, const std::shared_ptr<CobScript>& script)
        {
            auto left = addBattlePlayer(sim, "left");
            auto right = addBattlePlayer(sim, "right");
            defineBattleUnits(sim);

            TestRandom random(7u);
            for (int column = 0; column < 6; ++column)
            {
                for (int row = 0; row < 10; ++row)
                {
                    auto z = intToSimScalar((row * 64) - 288);
                    auto leftJitter = intToSimScalar((random.between(0, 2) - 1) * 16);
                    auto rightJitter = intToSimScalar((random.between(0, 2) - 1) * 16);

                    spawnBattleUnit(sim, left, SimVector(intToSimScalar(-32 - (column * 64)), 0_ss, z + leftJitter), script);
                    spawnBattleUnit(sim, right, SimVector(intToSimScalar(32 + (column * 64)), 0_ss, z + rightJitter), script);
                }
            }
        }

        /** Every unit's weapon target, in unit order: what the search decided. */
        std::vector<int> weaponTargets(const GameSimulation& sim)
        {
            std::vector<int> targets;
            for (const auto& [unitId, unit] : sim.units)
            {
                targets.push_back(static_cast<int>(unitId.value));
                const auto& weapon = unit.weapons[0];
                if (!weapon)
                {
                    targets.push_back(-1);
                    continue;
                }

                auto attacking = std::get_if<UnitWeaponStateAttacking>(&weapon->state);
                if (attacking == nullptr)
                {
                    targets.push_back(-2);
                    continue;
                }

                auto target = std::get_if<UnitId>(&attacking->target);
                targets.push_back(target == nullptr ? -3 : static_cast<int>(target->value));
            }
            return targets;
        }
    }

    TEST_CASE("the spatial index never hides a unit from a target search", "[targetsearch]")
    {
        // The search that used to walk every unit in the game now walks a
        // grid of them. Nothing about the answer may change, so this runs the
        // same battle twice: once with the search margin set so wide that
        // every unit on the map comes back as a candidate -- which is the old
        // whole-list walk, since the extra candidates are all rejected before
        // anything is drawn from the generator -- and once with the real one.
        //
        // Both what every weapon is pointed at and the whole simulation hash
        // are compared every tick, so the first tick on which a search misses
        // somebody, picks differently, or draws from the generator a different
        // number of times is the tick this fails on.
        auto script = makeBattleScript();

        GameSimulation reference(makeBattleTerrain(), 0u, 0, 0);
        // Nothing in this battle moves, but a margin this wide would cover it
        // if it did, and it puts every cell of the grid inside every query.
        reference.maxUnitSpeedPerTick = 1000000.0f;
        layOutBattle(reference, script);

        GameSimulation subject(makeBattleTerrain(), 0u, 0, 0);
        layOutBattle(subject, script);

        auto mostEverAiming = std::size_t{0};
        for (int tick = 0; tick < 150; ++tick)
        {
            reference.tick();
            subject.tick();

            INFO("diverged on tick " << tick);
            auto subjectTargets = weaponTargets(subject);
            REQUIRE(subjectTargets == weaponTargets(reference));
            REQUIRE(computeHashOf(subject).value == computeHashOf(reference).value);

            auto aiming = std::size_t{0};
            for (std::size_t i = 1; i < subjectTargets.size(); i += 2)
            {
                if (subjectTargets[i] >= 0)
                {
                    ++aiming;
                }
            }
            mostEverAiming = std::max(mostEverAiming, aiming);
        }

        // A battle in which nothing ever acquired anything would pass the
        // above without testing a thing.
        REQUIRE(mostEverAiming > 10);
    }

    TEST_CASE("a spatial index query is a superset of what is really in range", "[targetsearch]")
    {
        // The index holds positions as of the last rebuild and is queried
        // again later in the same tick, by which time units have moved. The
        // contract it has to keep is that a query widened by the margin
        // cannot miss a unit that has moved by less than the margin -- and
        // that what comes back is in ascending id order, because that is the
        // order the unit list itself iterates in and so the order the random
        // draws have to happen in.
        UnitSpatialIndex index;
        const auto margin = 64.0f;
        index.reset(-512.0f, -512.0f, 1024.0f, 1024.0f, margin);

        struct Placed
        {
            UnitId id;
            PlayerId owner;
            float x;
            float z;
        };

        TestRandom random(99u);
        std::vector<Placed> placed;
        for (int i = 0; i < 200; ++i)
        {
            // Ids shaped the way VectorMap shapes them, the slot in the high
            // bits: the order this test expects back is the order they were
            // made in. Some are placed off the map, which a unit can be.
            UnitId id((static_cast<unsigned int>(i) << 8u) | static_cast<unsigned int>(i % 7));
            PlayerId owner(static_cast<unsigned int>(i % 3));
            auto x = static_cast<float>(random.between(-700, 700));
            auto z = static_cast<float>(random.between(-700, 700));
            placed.push_back(Placed{id, owner, x, z});
            index.insert(id, owner, x, z);
        }
        index.build();

        for (int trial = 0; trial < 300; ++trial)
        {
            auto queryX = static_cast<float>(random.between(-700, 700));
            auto queryZ = static_cast<float>(random.between(-700, 700));
            auto radius = static_cast<float>(random.between(1, 900));
            PlayerId excluded(static_cast<unsigned int>(random.between(0, 2)));

            const auto& found = index.collectEnemiesNear(queryX, queryZ, radius, excluded);

            for (std::size_t i = 1; i < found.size(); ++i)
            {
                INFO("query results are not in ascending id order");
                REQUIRE(found[i - 1].value < found[i].value);
            }

            for (const auto& id : found)
            {
                auto it = std::find_if(placed.begin(), placed.end(), [&](const Placed& p) { return p.id == id; });
                REQUIRE(it != placed.end());
                INFO("the searcher's own side came back from the query");
                REQUIRE(it->owner != excluded);
            }

            for (const auto& p : placed)
            {
                if (p.owner == excluded)
                {
                    continue;
                }

                // Push the unit as far towards the query point as a tick of
                // movement could have taken it: that is the worst case the
                // margin exists to cover.
                auto toQueryX = queryX - p.x;
                auto toQueryZ = queryZ - p.z;
                auto distance = std::sqrt((toQueryX * toQueryX) + (toQueryZ * toQueryZ));
                auto closestItCouldBe = distance <= margin ? 0.0f : distance - margin;
                if (closestItCouldBe > radius)
                {
                    continue;
                }

                INFO("unit " << p.id.value << " could be within " << radius << " of the query and did not come back");
                REQUIRE(std::find(found.begin(), found.end(), p.id) != found.end());
            }
        }
    }
}
