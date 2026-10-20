#include <algorithm>
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
         * draw is screen-space quad vertices and nothing that wants a GL
         * context.
         */
        void addLaserWeapon(GameMediaDatabase& db, const std::string& weaponType, bool hasCompanion = true)
        {
            WeaponMediaInfo info;
            info.startSmoke = false;
            info.endSmoke = false;
            info.soundTrigger = false;
            info.renderType = ProjectileRenderTypeLaser{Vector3f(1.0f, 0.0f, 0.0f), Vector3f(0.5f, 0.0f, 0.0f), 4_ss, hasCompanion};
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

        /** Runs the real draw and reports how many triangle vertices it laid down. */
        std::size_t drawnTriangleVertexCount(const GameSimulation& sim, const PlayerVisibility& visibility, const GameMediaDatabase& db)
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
                UnitTextureAtlases{TextureIdentifier(), TextureIdentifier(), &teamAtlases, &teamAtlases},
                lines,
                sprites,
                meshes);
            return lines.triangles.size();
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
            REQUIRE(drawnTriangleVertexCount(sim, vis, db) == 0);
        }

        SECTION("ground walked once and left behind draws nothing either")
        {
            // Remembered ground keeps its terrain and the features that stood
            // on it. What is happening there now is not part of the memory.
            sim.explored.set(cell.x, cell.y, sim.losGroupBitFor(us));
            REQUIRE(drawnTriangleVertexCount(sim, vis, db) == 0);
        }

        SECTION("ground in sight draws the round")
        {
            sim.explored.set(cell.x, cell.y, sim.losGroupBitFor(us));
            vis.visible.set(cell.x, cell.y, 1);
            REQUIRE(drawnTriangleVertexCount(sim, vis, db) == 12);
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

        REQUIRE(drawnTriangleVertexCount(sim, vis, db) == 12);
    }

    TEST_CASE("effectIsVisibleToPlayer asks about live sight, not memory", "[fogeffects]")
    {
        GameSimulation sim(makeFogTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto& vis = sim.playerVisibility.at(us.value);

        SimVector position(0_ss, 0_ss, 0_ss);
        auto cell = sim.visionCellAt(position);

        REQUIRE_FALSE(effectIsVisibleToPlayer(sim, vis, position));

        sim.explored.set(cell.x, cell.y, sim.losGroupBitFor(us));
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

    TEST_CASE("a laser whose second colour is zero draws one line", "[fogeffects]")
    {
        // color2=0 suppresses the companion rather than drawing it black; the
        // ordinary LASERS.TDF laser is the shipped case
        // (LASER-WEAPON-FINDINGS.md section 4).
        GameSimulation sim(makeFogTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");

        GameMediaDatabase db;
        addLaserWeapon(db, "LASER", false);

        SimVector position(0_ss, 0_ss, 0_ss);
        addProjectileAt(sim, "LASER", position);

        auto& vis = sim.playerVisibility.at(us.value);
        auto cell = sim.visionCellAt(position);
        sim.explored.set(cell.x, cell.y, sim.losGroupBitFor(us));
        vis.visible.set(cell.x, cell.y, 1);

        REQUIRE(drawnTriangleVertexCount(sim, vis, db) == 6);
    }

    TEST_CASE("appendLaserBeam: a horizontal shot puts the companion one pixel above", "[fogeffects]")
    {
        // The core is one original pixel wide and the companion is one pixel
        // above it, so the pair covers exactly two screen units of Z
        // (LASER-WEAPON-FINDINGS.md section 2).
        std::vector<GlColoredVertex> out;
        appendLaserBeam(Vector3f(0.0f, 0.0f, 10.0f), Vector3f(20.0f, 0.0f, 10.0f), Vector3f(1.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), true, out);

        REQUIRE(out.size() == 12);

        float coreMinZ = 1.0e30f;
        float coreMaxZ = -1.0e30f;
        float companionMinZ = 1.0e30f;
        float companionMaxZ = -1.0e30f;
        for (const auto& v : out)
        {
            if (v.r > 0.5f)
            {
                coreMinZ = std::min(coreMinZ, v.z);
                coreMaxZ = std::max(coreMaxZ, v.z);
            }
            else
            {
                companionMinZ = std::min(companionMinZ, v.z);
                companionMaxZ = std::max(companionMaxZ, v.z);
            }
        }

        CHECK(coreMinZ == 9.5f);
        CHECK(coreMaxZ == 10.5f);
        CHECK(companionMinZ == 8.5f);
        CHECK(companionMaxZ == 9.5f);
    }

    TEST_CASE("appendLaserBeam: a steep shot gets the crossing companion", "[fogeffects]")
    {
        // One pixel left of the top end to one pixel right of the bottom end,
        // not a constant offset along the beam
        // (LASER-WEAPON-FINDINGS.md section 2).
        std::vector<GlColoredVertex> out;
        appendLaserBeam(Vector3f(10.0f, 0.0f, 0.0f), Vector3f(10.0f, 0.0f, 20.0f), Vector3f(1.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), true, out);

        REQUIRE(out.size() == 12);

        float coreMinX = 1.0e30f;
        float coreMaxX = -1.0e30f;
        float companionMinX = 1.0e30f;
        float companionMaxX = -1.0e30f;
        for (const auto& v : out)
        {
            if (v.r > 0.5f)
            {
                coreMinX = std::min(coreMinX, v.x);
                coreMaxX = std::max(coreMaxX, v.x);
            }
            else
            {
                companionMinX = std::min(companionMinX, v.x);
                companionMaxX = std::max(companionMaxX, v.x);
            }
        }

        CHECK(coreMinX >= 9.5f);
        CHECK(coreMaxX <= 10.5f);
        CHECK(companionMinX < 9.0f);
        CHECK(companionMaxX > 11.0f);
    }
}
