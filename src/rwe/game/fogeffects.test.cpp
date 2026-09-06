#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>
#include <rwe/sim/sim_test_util.h>

/**
 * What the fog of war is allowed to hide, beyond the units standing in it.
 *
 * Units were gated on line of sight from the start; the rounds flying between
 * them and the explosions they left behind were not, so a battle in unexplored
 * ground drew itself in full over a black map. The original refuses every
 * effect for a place the local player cannot see before it works anything else
 * out (0x480EEA, S:16), and builds its render list from the same sight
 * predicate with the radar bits never read (S:18).
 */
namespace rwe
{
    namespace
    {
        MapTerrain makeFogTerrain()
        {
            Grid<unsigned char> heights(32, 32, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /**
         * A weapon drawn as a laser. The laser is the render type that needs
         * nothing loaded -- no sprite, no model -- so what comes out of the
         * draw is two lines in a batch and nothing that wants a GL context.
         */
        void addLaserWeapon(GameMediaDatabase& db, const std::string& weaponType)
        {
            WeaponMediaInfo info;
            info.startSmoke = false;
            info.endSmoke = false;
            info.soundTrigger = false;
            info.renderType = ProjectileRenderTypeLaser{Vector3f(1.0f, 0.0f, 0.0f), Vector3f(0.5f, 0.0f, 0.0f), 4_ss};
            db.addWeapon(weaponType, std::move(info));
        }

        void addProjectileAt(GameSimulation& sim, const std::string& weaponType, const SimVector& position)
        {
            Projectile p;
            p.weaponType = weaponType;
            p.owner = PlayerId(0);
            p.position = position;
            p.previousPosition = position;
            p.origin = position;
            p.velocity = SimVector(1_ss, 0_ss, 0_ss);
            p.damageRadius = 0_ss;
            p.edgeEffectiveness = 0_ss;
            p.groundBounce = false;
            sim.projectiles.emplace(std::move(p));
        }

        /** Runs the real draw and reports how many line vertices it laid down. */
        std::size_t drawnLineCount(const GameSimulation& sim, const PlayerVisibility& visibility, const GameMediaDatabase& db)
        {
            ColoredMeshBatch lines;
            SpriteBatch sprites;
            UnitMeshBatch meshes;
            std::vector<SharedTextureHandle> teamAtlases;
            drawProjectiles(
                sim,
                visibility,
                db,
                Matrix4f::identity(),
                sim.projectiles,
                sim.gameTime,
                0.0f,
                TextureIdentifier(),
                teamAtlases,
                lines,
                sprites,
                meshes);
            return lines.lines.size();
        }

        WeaponMediaInfo makeImpactWeapon()
        {
            WeaponMediaInfo info;
            info.startSmoke = false;
            info.endSmoke = true;
            info.soundTrigger = false;
            info.renderType = ProjectileRenderTypeNone{};
            info.explosionAnim = AnimLocation{"FX", "explode3"};
            info.waterExplosionAnim = AnimLocation{"FX", "waterexplode"};
            return info;
        }
    }

    TEST_CASE("a missile over ground the player cannot see is not drawn", "[fogeffects]")
    {
        GameSimulation sim(makeFogTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");

        GameMediaDatabase db;
        addLaserWeapon(db, "LASER");

        SimVector position(0_ss, 0_ss, 0_ss);
        addProjectileAt(sim, "LASER", position);

        auto& vis = sim.playerVisibility.at(us.value);
        auto cell = sim.visionCellAt(position);
        REQUIRE(vis.contains(cell));

        SECTION("unexplored ground draws nothing")
        {
            REQUIRE(drawnLineCount(sim, vis, db) == 0);
        }

        SECTION("ground walked once and left behind draws nothing either")
        {
            // Remembered ground keeps its terrain and the features that stood
            // on it. What is happening there now is not part of the memory.
            vis.explored.set(cell.x, cell.y, 1);
            REQUIRE(drawnLineCount(sim, vis, db) == 0);
        }

        SECTION("ground in sight draws the round")
        {
            vis.explored.set(cell.x, cell.y, 1);
            vis.visible.set(cell.x, cell.y, 1);
            REQUIRE(drawnLineCount(sim, vis, db) == 4);
        }
    }

    TEST_CASE("only the round over dark ground is dropped", "[fogeffects]")
    {
        // The gate is per projectile, not per frame: a salvo half of which has
        // crossed into sight draws the half that has.
        GameSimulation sim(makeFogTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");

        GameMediaDatabase db;
        addLaserWeapon(db, "LASER");

        SimVector lit(0_ss, 0_ss, 0_ss);
        SimVector dark(-200_ss, 0_ss, -200_ss);
        addProjectileAt(sim, "LASER", lit);
        addProjectileAt(sim, "LASER", dark);

        auto& vis = sim.playerVisibility.at(us.value);
        auto litCell = sim.visionCellAt(lit);
        auto darkCell = sim.visionCellAt(dark);
        REQUIRE(litCell != darkCell);
        vis.visible.set(litCell.x, litCell.y, 1);

        REQUIRE(drawnLineCount(sim, vis, db) == 4);
    }

    TEST_CASE("effectIsVisibleToPlayer asks about live sight, not memory", "[fogeffects]")
    {
        GameSimulation sim(makeFogTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto& vis = sim.playerVisibility.at(us.value);

        SimVector position(0_ss, 0_ss, 0_ss);
        auto cell = sim.visionCellAt(position);

        REQUIRE_FALSE(effectIsVisibleToPlayer(sim, vis, position));

        vis.explored.set(cell.x, cell.y, 1);
        REQUIRE_FALSE(effectIsVisibleToPlayer(sim, vis, position));

        vis.visible.set(cell.x, cell.y, 1);
        REQUIRE(effectIsVisibleToPlayer(sim, vis, position));
    }

    TEST_CASE("effectIsVisibleToPlayer says no to a position off the map", "[fogeffects]")
    {
        // visionCellAt may hand back a cell outside the grid, and a bounds
        // check that answered "clear" would light every effect that strayed
        // over the edge.
        GameSimulation sim(makeFogTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto& vis = sim.playerVisibility.at(us.value);

        SimVector offMap(-100000_ss, 0_ss, -100000_ss);
        REQUIRE_FALSE(vis.contains(sim.visionCellAt(offMap)));
        REQUIRE_FALSE(effectIsVisibleToPlayer(sim, vis, offMap));
    }

    TEST_CASE("an explosion under the fog puts nothing on the screen", "[fogeffects]")
    {
        auto weapon = makeImpactWeapon();

        SECTION("out of sight: no sprite, no smoke, no flash")
        {
            auto effects = computeWeaponImpactEffects(weapon, ImpactType::Normal, false);
            REQUIRE_FALSE(effects.explosion.has_value());
            REQUIRE_FALSE(effects.smoke);
            REQUIRE_FALSE(effects.flash);
        }

        SECTION("a splash out of sight is withheld the same way")
        {
            auto effects = computeWeaponImpactEffects(weapon, ImpactType::Water, false);
            REQUIRE_FALSE(effects.explosion.has_value());
            REQUIRE_FALSE(effects.flash);
        }

        SECTION("in sight, a hit gets its sprite, its smoke and its flash")
        {
            auto effects = computeWeaponImpactEffects(weapon, ImpactType::Normal, true);
            REQUIRE(effects.explosion.has_value());
            REQUIRE(effects.explosion->animName == "explode3");
            REQUIRE(effects.smoke);
            REQUIRE(effects.flash);
        }

        SECTION("in sight, a splash gets the water art and no smoke")
        {
            // The original's smoke emitters check the point against the sea
            // level byte and emit nothing underwater (S:4).
            auto effects = computeWeaponImpactEffects(weapon, ImpactType::Water, true);
            REQUIRE(effects.explosion.has_value());
            REQUIRE(effects.explosion->animName == "waterexplode");
            REQUIRE_FALSE(effects.smoke);
            REQUIRE(effects.flash);
        }
    }

    TEST_CASE("a weapon with no art for the surface it hit still flashes", "[fogeffects]")
    {
        // Which is what it did before, and the flash is the part a player
        // reads as "something landed here".
        WeaponMediaInfo weapon;
        weapon.startSmoke = false;
        weapon.endSmoke = false;
        weapon.soundTrigger = false;
        weapon.renderType = ProjectileRenderTypeNone{};

        auto effects = computeWeaponImpactEffects(weapon, ImpactType::Normal, true);
        REQUIRE_FALSE(effects.explosion.has_value());
        REQUIRE_FALSE(effects.smoke);
        REQUIRE(effects.flash);
    }
}
