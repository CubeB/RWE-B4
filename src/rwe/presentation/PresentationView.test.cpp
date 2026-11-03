#include <catch2/catch_test_macros.hpp>
#include <rwe/presentation/PresentationView.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapFeature.h>
#include <rwe/sim/PlayerVisibility.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        UnitDefinition makeTestUnitDefinition(const std::string& name)
        {
            UnitDefinition definition{};
            definition.unitName = name;
            definition.unitDescription = name + " description";
            definition.objectName = "model";
            definition.maxHitPoints = 250;
            definition.buildTime = 120;
            definition.buildCostMetal = Metal(40.0f);
            definition.buildCostEnergy = Energy(80.0f);
            definition.isMobile = true;
            definition.canMove = true;
            definition.builder = false;
            // The visibility predicate asks for the footprint; a default
            // variant names a movement class the test database does not hold.
            definition.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2, 2, 255u, 255u, 0u, 0u};
            return definition;
        }

        void markVisible(GameSimulation& sim, PlayerId viewer, const SimVector& position)
        {
            // The unit predicate samples the corners of the model's bounding
            // box, not only its centre, and a corner can fall in the next
            // vision cell. Light the whole 3x3 block so the fixture is about
            // the filter, not about where a 64-unit cell boundary happened.
            auto cell = sim.visionCellAt(position);
            auto& visibility = sim.playerVisibility.at(viewer.value);
            for (int dx = -1; dx <= 1; ++dx)
            {
                for (int dz = -1; dz <= 1; ++dz)
                {
                    if (visibility.contains(Point(cell.x + dx, cell.y + dz)))
                    {
                        visibility.visible.set(cell.x + dx, cell.y + dz, 1);
                    }
                }
            }
        }

        void markExplored(GameSimulation& sim, PlayerId viewer, const SimVector& position)
        {
            auto cell = sim.visionCellAt(position);
            sim.explored.set(cell.x, cell.y, static_cast<ExploredMask>(sim.losGroupBitFor(viewer)));
        }
    }

    TEST_CASE("[presentation] the player view never returns an enemy the viewer cannot see", "[presentation][modsdk]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto viewer = addPlayer(sim, "viewer");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["TEST"] = makeTestUnitDefinition("Test Unit");
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, {});

        auto own = addUnitOfType(sim, "TEST", viewer, SimVector(100_ss, 0_ss, 100_ss), makeEmptyCobScript());
        auto hidden = addUnitOfType(sim, "TEST", enemy, SimVector(900_ss, 0_ss, 900_ss), makeEmptyCobScript());
        auto seen = addUnitOfType(sim, "TEST", enemy, SimVector(400_ss, 0_ss, 400_ss), makeEmptyCobScript());

        markVisible(sim, viewer, sim.getUnitState(seen).position);

        auto view = PlayerView(sim, viewer, sim.playerVisibility.at(viewer.value));

        auto units = view.units();
        REQUIRE(units.size() == 2);

        bool sawOwn = false;
        bool sawSeen = false;
        for (const auto& unit : units)
        {
            sawOwn = sawOwn || unit.id == own;
            sawSeen = sawSeen || unit.id == seen;
            REQUIRE(unit.id != hidden);
        }
        REQUIRE(sawOwn);
        REQUIRE(sawSeen);

        REQUIRE(view.unit(own).has_value());
        REQUIRE(view.unit(seen).has_value());
        REQUIRE_FALSE(view.unit(hidden).has_value());
    }

    TEST_CASE("[presentation] the player view gives a sensor contact no identity or health", "[presentation][modsdk]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto viewer = addPlayer(sim, "viewer");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["TEST"] = makeTestUnitDefinition("Test Unit");
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, {});

        auto contact = addUnitOfType(sim, "TEST", enemy, SimVector(800_ss, 0_ss, 800_ss), makeEmptyCobScript());
        sim.playerVisibility.at(viewer.value).radarContacts.insert(contact);

        auto view = PlayerView(sim, viewer, sim.playerVisibility.at(viewer.value));

        // A contact is not a unit view: asking by id still gives nothing.
        REQUIRE_FALSE(view.unit(contact).has_value());
        REQUIRE(view.units().empty());

        auto contacts = view.sensorContacts();
        REQUIRE(contacts.size() == 1);
        REQUIRE(contacts.front().id == contact);
        REQUIRE(contacts.front().position.x == sim.getUnitState(contact).position.x);
        REQUIRE(contacts.front().position.z == sim.getUnitState(contact).position.z);
        REQUIRE_FALSE(contacts.front().sonar);
    }

    TEST_CASE("[presentation] the player view hides unexplored features and lights only visible projectiles", "[presentation][modsdk]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto viewer = addPlayer(sim, "viewer");
        auto enemy = addPlayer(sim, "enemy");

        FeatureDefinition rock{};
        rock.name = "rock";
        auto rockId = sim.featureDefinitions.insert(rock);

        MapFeature known{rockId, SimVector(200_ss, 0_ss, 200_ss)};
        MapFeature remembered{rockId, SimVector(300_ss, 0_ss, 300_ss)};
        MapFeature unknown{rockId, SimVector(700_ss, 0_ss, 700_ss)};
        auto knownId = sim.features.emplace(known);
        auto rememberedId = sim.features.emplace(remembered);
        auto unknownId = sim.features.emplace(unknown);

        markExplored(sim, viewer, known.position);
        markVisible(sim, viewer, known.position);
        markExplored(sim, viewer, remembered.position);

        Projectile knownRound{};
        knownRound.owner = enemy;
        knownRound.position = known.position;
        Projectile hiddenRound{};
        hiddenRound.owner = enemy;
        hiddenRound.position = unknown.position;
        auto knownRoundId = sim.projectiles.emplace(knownRound);
        sim.projectiles.emplace(hiddenRound);

        auto view = PlayerView(sim, viewer, sim.playerVisibility.at(viewer.value));

        auto features = view.features();
        REQUIRE(features.size() == 2);

        bool sawKnown = false;
        bool sawRemembered = false;
        for (const auto& feature : features)
        {
            REQUIRE(feature.id != unknownId);
            REQUIRE(feature.type == "rock");
            if (feature.id == knownId)
            {
                sawKnown = true;
                REQUIRE(feature.visible);
            }
            if (feature.id == rememberedId)
            {
                sawRemembered = true;
                REQUIRE_FALSE(feature.visible);
            }
        }
        REQUIRE(sawKnown);
        REQUIRE(sawRemembered);

        auto rounds = view.projectiles();
        REQUIRE(rounds.size() == 1);
        REQUIRE(rounds.front().id == knownRoundId);
        REQUIRE(view.isExploredAt(known.position));
        REQUIRE(view.isVisibleAt(known.position));
        REQUIRE_FALSE(view.isExploredAt(unknown.position));
    }

    TEST_CASE("[presentation] the player view exposes the viewer's stockpile and static type data only", "[presentation][modsdk]")
    {
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto viewer = addPlayer(sim, "viewer");
        addPlayer(sim, "enemy");
        sim.unitDefinitions["TEST"] = makeTestUnitDefinition("Test Unit");

        auto view = PlayerView(sim, viewer, sim.playerVisibility.at(viewer.value));

        auto resources = view.resources();
        REQUIRE(resources.has_value());
        REQUIRE(resources->metal == Metal(1000.0f));
        REQUIRE(resources->energy == Energy(1000.0f));
        REQUIRE(resources->maxMetal == Metal(1000.0f));
        REQUIRE(resources->maxEnergy == Energy(1000.0f));

        auto definition = view.unitDefinition("TEST");
        REQUIRE(definition.has_value());
        REQUIRE(definition->displayName == "Test Unit");
        REQUIRE(definition->buildCostMetal == Metal(40.0f));
        REQUIRE(definition->buildCostEnergy == Energy(80.0f));
        REQUIRE(definition->buildTime == 120);
        REQUIRE(definition->maxHitPoints == 250);

        REQUIRE_FALSE(view.unitDefinition("NOPE").has_value());
    }
}
