#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <rwe/io/tad/tad_events.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/UnitWeapon.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>
#include <rwe/sim/tad_weapon_episodes.h>
#include <string>
#include <variant>

// Conformance cases: how long a projectile takes to reach what it was aimed at,
// against how long it took in a real Total Annihilation game.
//
// The episodes are in tad_weapon_episodes.h, mined by tad_episodes
// --emit-weapon-cpp and regenerated rather than edited. The neighbouring
// hand-written cases -- missile.test.cpp, ballisticair.test.cpp,
// interception.test.cpp, accuracy.test.cpp -- choose their numbers to exercise
// a behaviour. EVERY NUMBER HERE CAME OUT OF A GAME. That is the difference to
// keep in mind when reading a failure: a hand-written case failing means the
// behaviour moved, and one of these failing means the behaviour no longer
// matches what the original did.
//
// THREE CLASSES ARE HERE. Rounds that fly at a constant speed; rounds with a
// motor -- a missile leaves at `startvelocity` and works up to `weaponvelocity`,
// and the episode carries both along with what times the burn; and rounds that
// are lobbed, which get no direction from the test at all. A ballistic episode
// hands the engine's OWN firing solution the muzzle and the aim point and fires
// along whatever it returns, so what the case pins is not a speed but
// computeBallisticHeadingAndPitch itself -- the flat root, the gravity it solves
// against, and the cosine that falls out of the angle.
//
// ONE EPISODE'S WEAPON CARRIES `cruise`, and it is in the motor class rather
// than a class of its own, because the flag is inert for it: the cruise clause
// is read only through the aim point and the aim point only from the guidance
// step, which a round with no `guidance` and no `turnrate` never takes. What
// the corpus can and cannot say about that is worth knowing before trusting it
// -- every episode here is inside the 1024-unit handover, so turning `cruise`
// AND `guidance` on in defineWeapon moves none of them. The episode pins the
// round's speed and where it stops, as every other one does, and it pins
// nothing about cruising. A vertical launch, a torpedo and a burst weapon are
// each a different flight and none of them is here; see the fixture header.
//
// WHAT IS DRIVEN. The real GameSimulation and the real Projectile, spawned by
// spawnProjectile and stepped by tick(), fired at a real victim standing at the
// aim point with the victim's own footprint, because what the corpus recorded is
// a projectile crossing a distance until it hits something and not a unit
// deciding to fire. Target selection, the aim and the reload are out of scope
// here and have their own tests.
//
// Arrival is the tick the projectile detonates on the victim. That is where TA
// stops it -- the first tick its move puts it in a map square the victim
// occupies (0x49B090), which is about half a footprint short of the aim point
// and not at it -- and it is what the demo's damage record marks. RWE moves a
// projectile and then tests the occupied grid, in the same order, so the tick
// count is the flight time with nothing added or taken away.
//
// WHAT THIS DOES NOT PIN. The projectile is spawned outside tick() and the
// ticks are counted from there, so the step count and the stop are pinned and
// where the FIRING sits relative to the first step is not. The original takes
// that first step on the tick it fires, and an episode's flightTicks is a tick
// wider than the flight because a demo stamps a shot from the other side of its
// tick's 0x2c -- the two conventions cancel, which is why the numbers below
// still stand. weaponfiretick.test.cpp is the one that pins the firing tick,
// through the whole pipeline; see docs/TA-DEMOS.md, "Which tick a round first
// moves on".

namespace rwe
{
    namespace
    {
        /**
         * Map squares on a side. Large enough that no episode's geometry leaves
         * it, flat at zero so nothing intersects the ground. A shot's real map
         * is not reproducible here and is not what is being measured.
         */
        constexpr int MapSquares = 1024;

        /**
         * Where a demo coordinate is in RWE's world. TA measures from the map's
         * corner and RWE from its centre, and the difference is half the map --
         * a whole number of sixteen-unit squares, so a square boundary in the
         * demo is a square boundary here, which is the property the footprint
         * test depends on.
         */
        SimScalar toWorld(int32_t fixed)
        {
            return SimScalar(static_cast<float>(tadFixedToDouble(fixed) - MapSquares * 8.0));
        }

        /**
         * The weapon as the loader would build it from the episode's own TDF
         * values -- the same weaponVelocity / 30 conversion as
         * LoadingScene_util.cpp, which is the one number this test is about.
         *
         * WHICH PHYSICS. Whatever the weapon's own `selfprop` says, because that
         * is what the engine tests first (0x49B9C2) and most missiles carry
         * `lineofsight=1` as well. So a round with a motor is flown by the
         * motor here too: it leaves at startVelocity, gains acceleration / 900 a
         * tick up to the same cap while the motor runs, and coasts after.
         *
         * WHAT IS NOT TRANSCRIBED, and why it cannot matter. `guidance`,
         * `tracks` and `turnrate` steer the round towards where its target IS,
         * and every episode here is a shot at a point the model measures to
         * along a heading the round is already on -- which is no accident, it is
         * what the self-propelled cells' victim bound selects for. Steering
         * towards a point already dead ahead is an identity, so the episode does
         * not carry fields that could only be set to values with no effect.
         *
         * AND `cruise` IS OFF FOR THE SAME REASON, which is worth spelling out
         * because one episode's weapon declares it. The cruise clause lives in
         * the aim point (0x49B3E0) and the aim point is asked for only from the
         * guidance step, so a round that cannot steer never reaches it. The
         * miner will not emit a cruise cell whose weapon can steer -- such a
         * weapon is classed "cruise steering" and left unscored -- so these
         * three zeroes are a transcription of ROCKET_HRK, which names none of
         * the three, and not an approximation of it.
         *
         * THE RANGE IS REAL FOR A MOTOR AND FAKE FOR A BEAM. A self-propelled
         * round has no `dieOnFrame` at all and its range is what times the burn,
         * so it gets the weapon's own. A line-of-sight one lives `range /
         * velocity` ticks, and several episodes are shots at the very edge of
         * their weapon's range where that expiry and the arrival fall on the
         * same tick; the flight time is not what that would be measuring, so
         * those keep a range nothing can reach.
         */
        void defineWeapon(GameSimulation& sim, const TadWeaponEpisode& episode)
        {
            WeaponDefinition w{};
            w.maxRange = 32000_ss;
            w.reloadTime = SimScalar(1.0f);
            w.burst = 1;
            w.burstInterval = 0_ss;
            w.velocity = SimScalar(static_cast<float>(episode.weaponVelocity)) / 30_ss;
            w.damageRadius = 16_ss;
            w.damage["DEFAULT"] = episode.weaponDamage;
            w.physicsType = ProjectilePhysicsTypeLineOfSight();

            if (episode.ballistic)
            {
                // A shell gets no `dieOnFrame` at all: createProjectileFromWeapon
                // works a life out only for a line-of-sight round, and falls back
                // on `weapontimer` otherwise -- which neither of these weapons
                // names. So the range here is inert, and the round lives until it
                // hits something.
                w.physicsType = ProjectilePhysicsTypeBallistic();
            }

            if (episode.selfPropelled)
            {
                w.maxRange = SimScalar(static_cast<float>(episode.weaponRange));
                w.randomDecay = GameTime(0);
                if (episode.weaponTimerTicks != 0)
                {
                    w.weaponTimer = GameTime(episode.weaponTimerTicks);
                }

                ProjectilePhysicsTypeSelfPropelled p;
                p.startVelocity = SimScalar(static_cast<float>(episode.startVelocity)) / 30_ss;
                p.acceleration = SimScalar(static_cast<float>(episode.weaponAcceleration)) / 900_ss;
                p.maxVelocity = SimScalar(static_cast<float>(episode.weaponVelocity)) / 30_ss;
                p.turnRate = SimAngle(0);
                p.guidance = false;
                p.tracks = false;
                p.twoPhase = false;
                p.vLaunch = false;
                p.flightTime = GameTime(0);
                p.burnBlow = episode.burnBlow;
                p.cruise = false;
                p.autoRange = !episode.noAutoRange && episode.weaponVelocity != 0;
                w.physicsType = p;
            }

            sim.weaponDefinitions["corpus"] = w;
        }

        SimVector originOf(const TadWeaponEpisode& e)
        {
            return SimVector(toWorld(e.originX), SimScalar(static_cast<float>(tadFixedToDouble(e.originY))), toWorld(e.originZ));
        }

        SimVector targetOf(const TadWeaponEpisode& e)
        {
            return SimVector(toWorld(e.targetX), SimScalar(static_cast<float>(tadFixedToDouble(e.targetY))), toWorld(e.targetZ));
        }

        /**
         * The victim, standing at the aim point on the ground with its own FBI
         * footprint, stamped into the occupied grid by tryAddUnit -- the real
         * spawn path, and the same computeFootprintRegion the miner's model
         * ports.
         *
         * ITS HEIGHT IS NOT WHAT IS MEASURED. The collision test has a second
         * half, that the round be below the victim's model top, and the corpus
         * model does not score it; so the victim is given a model tall enough
         * that no episode's round passes over it, and only the footprint can
         * decide the tick.
         */
        UnitId addVictim(GameSimulation& sim, const TadWeaponEpisode& episode, PlayerId owner)
        {
            UnitDefinition d{};
            d.objectName = "corpus_victim";
            d.isMobile = true;
            d.maxHitPoints = 1000000;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{
                episode.victimFootprintX, episode.victimFootprintZ, 255u, 255u, 0u, 255u};
            sim.unitDefinitions["CORPUS_VICTIM"] = d;

            UnitModelDefinition model{};
            model.height = 100000_ss;
            sim.unitModelDefinitions["corpus_victim"] = model;

            auto script = makeEmptyCobScript();
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            UnitState unit(pieces, std::move(env));
            unit.unitType = "CORPUS_VICTIM";
            unit.owner = owner;
            auto at = targetOf(episode);
            unit.position = SimVector(at.x, 0_ss, at.z);
            unit.previousPosition = unit.position;
            unit.hitPoints = 1000000;
            unit.buildTimeCompleted = 0;
            auto id = sim.tryAddUnit(std::move(unit));
            REQUIRE(id.has_value());
            return *id;
        }

        /**
         * How many ticks until the projectile detonates on the victim, or zero
         * if it never does -- including if it goes off anywhere else, which
         * would be a hit on something that is not being measured.
         */
        unsigned int ticksToHit(const TadWeaponEpisode& episode)
        {
            GameSimulation sim(makeFlatTerrain(MapSquares, MapSquares), 0u, 0, 0);
            auto shooter = addPlayer(sim, "shooter");
            auto target = addPlayer(sim, "target");
            defineWeapon(sim, episode);
            auto victimId = addVictim(sim, episode, target);
            auto footprint = sim.computeFootprintRegion(
                sim.getUnitState(victimId).position, episode.victimFootprintX, episode.victimFootprintZ);

            auto from = originOf(episode);
            auto at = targetOf(episode);
            auto toTarget = at - from;
            auto distance = toTarget.length();

            // WHERE A SHELL IS POINTED IS THE WHOLE OF THE CASE. Everything else
            // here flies at the aim point, but a lobbed round leaves the barrel
            // along an angle the engine SOLVES for, and that solution -- the flat
            // root of the ballistic quadratic, against the same 112/900 gravity
            // the projectile pass then applies -- is what the corpus is being
            // held to. So the direction comes from the same two calls
            // tryFireWeapon makes, with the shooter facing north and no
            // `ballisticZOffset`, neither of which a demo records and neither of
            // which can change the angle: the heading has the rotation taken off
            // and added straight back on, and the offset is a muzzle position on
            // a model this test does not build.
            auto direction = toTarget.normalized();
            if (episode.ballistic)
            {
                auto headingAndPitch = computeHeadingAndPitch(
                    SimAngle(0), from, at, sim.weaponDefinitions.at("corpus").velocity,
                    (112_ss / (30_ss * 30_ss)), 0_ss, sim.weaponDefinitions.at("corpus").physicsType);
                direction = toDirection(headingAndPitch.first, -headingAndPitch.second);
            }

            UnitWeapon weapon;
            weapon.weaponType = "corpus";
            sim.spawnProjectile(ProjectileSpawn{
                .owner = shooter,
                .weapon = &weapon,
                .position = from,
                .direction = direction,
                .distanceToTarget = distance,
                .targetUnit = std::nullopt,
                .attacker = std::nullopt,
                .inheritedVelocity = std::nullopt,
                .targetPosition = at});

            for (unsigned int ticks = 1; ticks <= 4000u; ++ticks)
            {
                sim.events.clear();
                sim.tick();
                for (const auto& e : sim.events)
                {
                    const auto* died = std::get_if<ProjectileDiedEvent>(&e);
                    if (died == nullptr)
                    {
                        continue;
                    }
                    auto square = sim.terrain.worldToHeightmapCoordinate(died->position);
                    auto onVictim = died->deathType == ProjectileDiedEvent::DeathType::NormalImpact
                        && footprint.contains(square);
                    return onVictim ? ticks : 0;
                }
            }

            return 0;
        }

        std::string episodeName(const TadWeaponEpisode& episode)
        {
            return std::string(episode.shooterName) + " slot " + std::to_string(episode.weaponSlot)
                + " (" + episode.weaponName + ", " + episode.demo + " tick "
                + std::to_string(episode.shotTick) + "-" + std::to_string(episode.damageTick) + ")";
        }
    }

    TEST_CASE("a shot takes as long to arrive as it did in a real game", "[weapon][corpus]")
    {
        for (const auto& episode : tadWeaponEpisodes)
        {
            DYNAMIC_SECTION(episodeName(episode))
            {
                auto ticks = ticksToHit(episode);
                REQUIRE(ticks > 0u);

                REQUIRE(static_cast<int>(ticks)
                    == static_cast<int>(episode.flightTicks) + episode.expectedFlightDelta);
            }
        }
    }

    TEST_CASE("every weapon episode is a shot with damage the weapon can explain", "[weapon][corpus]")
    {
        // Not about RWE at all: this guards the fixture. The pairing that
        // produced these episodes is a filter over unlinked events, and the one
        // piece of evidence that it pairs the right damage with the right shot
        // is that each cell's modal damage is the firing weapon's own [DAMAGE]
        // default. A regeneration that broke the pairing would still emit
        // plausible-looking rows, and this is what would notice.
        for (const auto& episode : tadWeaponEpisodes)
        {
            DYNAMIC_SECTION(episodeName(episode))
            {
                REQUIRE(episode.weaponVelocity > 0u);
                REQUIRE(episode.weaponDamage > 0u);

                // A victim with no footprint occupies no square and nothing
                // could hit it; a row that had one would be the miner scoring a
                // pairing its model cannot stop.
                REQUIRE(episode.victimFootprintX > 0u);
                REQUIRE(episode.victimFootprintZ > 0u);

                // What the round leaves the barrel at, asked the way
                // createProjectileFromWeapon asks it: a startVelocity of zero is
                // full speed for a weapon with no motor and a standstill for one
                // with a motor, so the field alone does not answer it. A row
                // that leaves below its cap has to be one the engine flies with
                // a motor -- otherwise the miner classified it one way and this
                // test flies it another.
                auto launchSpeed = episode.startVelocity != 0u
                    ? episode.startVelocity
                    : (episode.weaponAcceleration == 0u ? episode.weaponVelocity : 0u);
                if (launchSpeed != episode.weaponVelocity)
                {
                    REQUIRE(episode.selfPropelled);
                    REQUIRE(episode.weaponAcceleration > 0u);
                    REQUIRE(launchSpeed < episode.weaponVelocity);
                }
                REQUIRE(episode.damageTick > episode.shotTick);
                REQUIRE(episode.flightTicks == episode.damageTick - episode.shotTick);
                REQUIRE(episode.pairingsAtMode <= episode.pairings);
                REQUIRE(episode.pairings >= 30u);

                // A representative that stood alone in its cell would be an
                // anecdote rather than a mode.
                REQUIRE(episode.pairingsAtMode > 1u);
            }
        }
    }
}
