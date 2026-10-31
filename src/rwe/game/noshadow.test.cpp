#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        /**
         * A factory holding a half-built mobile product, plus the same model
         * standing on its own as an ordinary standalone nanoframe.
         */
        struct AttachedProductFixture
        {
            GameSimulation sim{makeFlatTerrain(64, 64), 0, 0, 0};
            PlayerId owner;
            UnitId factory;
            UnitId product;
            UnitId standalone;

            AttachedProductFixture()
            {
                UnitDefinition factoryDefinition{};
                factoryDefinition.objectName = "TESTLAB";
                factoryDefinition.builder = true;
                factoryDefinition.buildTime = 1000;
                sim.unitDefinitions["TESTLAB"] = factoryDefinition;

                UnitDefinition productDefinition{};
                productDefinition.objectName = "TESTSUB";
                productDefinition.isMobile = true;
                productDefinition.buildTime = 1000;
                sim.unitDefinitions["TESTSUB"] = productDefinition;

                auto script = makeEmptyCobScript();
                for (const auto& name : {"TESTLAB", "TESTSUB"})
                {
                    sim.unitScriptDefinitions[name] = *script;
                    sim.unitModelDefinitions[name] = createUnitModelDefinition(10_ss, {});
                }

                owner = addPlayer(sim);
                factory = addUnitOfType(sim, "TESTLAB", owner, SimVector(0_ss, 0_ss, 0_ss), script);
                product = addUnitOfType(sim, "TESTSUB", owner, SimVector(0_ss, 0_ss, 0_ss), script);
                standalone = addUnitOfType(sim, "TESTSUB", owner, SimVector(100_ss, 0_ss, 0_ss), script);

                sim.getUnitState(product).buildTimeCompleted = 500;
                sim.getUnitState(standalone).buildTimeCompleted = 500;
                hold();
            }

            void hold()
            {
                sim.getUnitState(factory).factoryState = FactoryBehaviorStateBuilding{std::make_pair(product, std::nullopt)};
            }
        };
    }

    TEST_CASE("unitCastsShadow: NoShadow takes the shadow away", "[noshadow]")
    {
        // Both of the original's shadow passes open with the same test on bit
        // 25 of the definition's flag word and skip the whole draw when it is
        // set: 0x4592A6 loads def+0x241 and 0x4592AC tests 0x2000000, and
        // 0x4594BA / 0x4594C0 do it again for the second path. Fifteen of the
        // shipped units name the key.
        UnitDefinition d{};
        REQUIRE(unitCastsShadow(d));

        d.noShadow = true;
        REQUIRE_FALSE(unitCastsShadow(d));
    }

    TEST_CASE("featureCastsShadow: a feature on ground below the sea level casts none", "[noshadow]")
    {
        // 0x4592D5-0x4592F1: for the Feature Unit (type index 0) the ground
        // under the feature is compared against the sea level byte and a
        // `jl` skips the shadow. Below casts nothing; at or above casts.
        REQUIRE(featureCastsShadow(30_ss, 20_ss));
        REQUIRE(featureCastsShadow(20_ss, 20_ss));
        REQUIRE_FALSE(featureCastsShadow(19_ss, 20_ss));
        REQUIRE_FALSE(featureCastsShadow(0_ss, 20_ss));
    }

    TEST_CASE("attachedFactoryProducts: only the product the factory is still building is attached", "[noshadow]")
    {
        // A factory product is attached to the build piece and drawn in the
        // factory's composite, whose path never runs the child's own shadow
        // calculation (SUBMARINE-CONSTRUCTION-SHADOW-FINDINGS.md section 1).
        // The standalone nanoframe beside it was never attached and keeps its
        // shadow.
        AttachedProductFixture f;

        auto attached = attachedFactoryProducts(f.sim);
        REQUIRE(attached.count(f.product) == 1);
        REQUIRE(attached.count(f.standalone) == 0);
        REQUIRE(attached.count(f.factory) == 0);
    }

    TEST_CASE("attachedFactoryProducts: the final build step releases the product", "[noshadow]")
    {
        // Release happens in the final successful build step, not at the
        // factory's later bookkeeping stage, so a completed product still
        // named by the factory is independently drawn already.
        AttachedProductFixture f;
        f.sim.getUnitState(f.product).buildTimeCompleted = f.sim.unitDefinitions.at("TESTSUB").buildTime;

        REQUIRE(attachedFactoryProducts(f.sim).empty());
    }

    TEST_CASE("attachedFactoryProducts: a factory that is not building attaches nothing", "[noshadow]")
    {
        AttachedProductFixture f;

        SECTION("idle")
        {
            f.sim.getUnitState(f.factory).factoryState = FactoryBehaviorStateIdle{};
            REQUIRE(attachedFactoryProducts(f.sim).empty());
        }

        SECTION("creating its next product")
        {
            f.sim.getUnitState(f.factory).factoryState = FactoryBehaviorStateCreatingUnit{
                "TESTSUB", f.owner, SimVector{}, SimAngle{}};
            REQUIRE(attachedFactoryProducts(f.sim).empty());
        }
    }

    TEST_CASE("unitShadowIsWaterCut: only a unit strictly below the sea is cut", "[noshadow]")
    {
        // A mobile unit whose origin is below the sea has its copied
        // silhouette cut at the water line, so a fully submerged one casts no
        // shadow (SUBMARINE-CONSTRUCTION-SHADOW-FINDINGS.md section 2).
        REQUIRE(unitShadowIsWaterCut(19.0f, 20.0f));
        REQUIRE_FALSE(unitShadowIsWaterCut(20.0f, 20.0f));
        REQUIRE_FALSE(unitShadowIsWaterCut(21.0f, 20.0f));
    }
}
