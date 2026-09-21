#include "GameSimulation.h"

#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/util/Index.h>
#include <rwe/util/collection_util.h>
#include <rwe/util/match.h>

#include <algorithm>
#include <vector>

namespace rwe
{
    namespace
    {
        /**
         * How long a unit stays visible after it fires or is hit. Used only
         * by updateCloakSuppression, which is why it lives here rather than
         * beside the blocked-site constants in GameSimulation.cpp.
         */
        constexpr unsigned int CloakSuppressionTicks = 90;

        /**
         * Whether any of the dishes or jammers of the given kind reaches the
         * point, measured in the map plane. Both lists carry their radius
         * already squared.
         */
        template <typename T>
        bool isInRangeOfAny(const std::vector<T>& sources, const SimVector& position, bool sonar)
        {
            for (const auto& source : sources)
            {
                if (source.sonar != sonar)
                {
                    continue;
                }

                auto dx = position.x - source.position.x;
                auto dz = position.z - source.position.z;
                if (((dx * dx) + (dz * dz)) <= source.rangeSquared)
                {
                    return true;
                }
            }

            return false;
        }
    }

    PlayerId GameSimulation::addPlayer(const GamePlayerInfo& info)
    {
        PlayerId id(players.size());
        players.push_back(info);
        playerLosGroupBits.push_back(nextLosGroupBit());

        const auto& heights = terrain.getHeightMap();
        auto cells = PlayerVisibility::VisionCellSizeInTiles;
        auto& vis = playerVisibility.emplace_back((heights.getWidth() + cells - 1) / cells, (heights.getHeight() + cells - 1) / cells);

        if (mappingMode == MappingMode::Mapped)
        {
            // Mapped gives the ground away before the game starts, and only
            // the ground: the map comes up explored but unlit, so terrain is
            // drawn in memory grey and anything standing on it stays hidden
            // until something actually looks at it.
            vis.exploreAll(ExploredMark{&explored, losGroupBitFor(id)});
        }

        return id;
    }

    ExploredMask GameSimulation::nextLosGroupBit() const
    {
        // The player just appended shares a bit with the earliest earlier
        // player on its team, or opens the lowest bit nobody is using yet.
        // Scanning in player order and handing out the lowest free bit makes
        // the assignment a pure function of the player list, so every peer --
        // and a load replaying the same players in the same order -- comes out
        // with the same bits.
        const auto& team = players.back().teamId;
        if (team.has_value())
        {
            for (std::size_t i = 0; i + 1 < players.size(); ++i)
            {
                const auto& other = players[i].teamId;
                if (other.has_value() && *other == *team)
                {
                    return playerLosGroupBits[i];
                }
            }
        }

        ExploredMask used = 0;
        for (auto bit : playerLosGroupBits)
        {
            used |= bit;
        }
        ExploredMask bit = 1;
        while ((used & bit) != 0)
        {
            bit = static_cast<ExploredMask>(bit << 1);
        }
        return bit;
    }

    ExploredMask GameSimulation::losGroupBitFor(PlayerId player) const
    {
        if (player.value < playerLosGroupBits.size())
        {
            return playerLosGroupBits[player.value];
        }

        // Not a player this simulation has. Answer with a bit no real group
        // can hold so a stray query explores nothing rather than someone
        // else's ground.
        return 0;
    }

    namespace
    {
        int heightMapSampleAt(const Grid<unsigned char>& heights, const SimVector& tile)
        {
            auto x = static_cast<int>(std::floor(tile.x.value));
            auto y = static_cast<int>(std::floor(tile.z.value));
            if (x < 0 || y < 0 || x >= heights.getWidth() || y >= heights.getHeight())
            {
                return 0;
            }

            return static_cast<int>(heights.get(x, y));
        }
    }

    int GameSimulation::terrainSampleHeightAt(const SimVector& position) const
    {
        return heightMapSampleAt(terrain.getHeightMap(), terrain.worldToHeightmapSpace(position));
    }

    Point GameSimulation::visionCellAt(const SimVector& position) const
    {
        // The projected-space transform. See the doc comment in the header:
        // the fog renderer must apply exactly this.
        auto tile = terrain.worldToHeightmapSpace(position);
        return heightmapToVisionCell(tile.x, tile.z, heightMapSampleAt(terrain.getHeightMap(), tile));
    }

    bool GameSimulation::isExploredBy(PlayerId player, const SimVector& position) const
    {
        auto cell = visionCellAt(position);
        if (cell.x < 0 || cell.y < 0 || cell.x >= explored.getWidth() || cell.y >= explored.getHeight())
        {
            return false;
        }
        return (explored.get(cell.x, cell.y) & losGroupBitFor(player)) != 0;
    }

    bool GameSimulation::isExploredByAnyGroup(const SimVector& position) const
    {
        auto cell = visionCellAt(position);
        if (cell.x < 0 || cell.y < 0 || cell.x >= explored.getWidth() || cell.y >= explored.getHeight())
        {
            return false;
        }
        return explored.get(cell.x, cell.y) != 0;
    }

    void GameSimulation::clearPlayers()
    {
        players.clear();
        playerVisibility.clear();
        playerLosGroupBits.clear();

        // The explored grid belongs to the players whose groups own its bits,
        // so it goes with them. Leaving it set would hand a fresh game the
        // ground the last one had walked.
        std::fill(explored.getVector().begin(), explored.getVector().end(), static_cast<ExploredMask>(0));
    }

    bool GameSimulation::arePlayersAllied(PlayerId a, PlayerId b) const
    {
        if (a == b)
        {
            return true;
        }
        const auto& first = players.at(a.value).teamId;
        const auto& second = players.at(b.value).teamId;
        return first.has_value() && second.has_value() && *first == *second;
    }

    bool GameSimulation::isVisibleTo(PlayerId player, const SimVector& position) const
    {
        return playerVisibility.at(player.value).isVisible(visionCellAt(position));
    }

    bool GameSimulation::isOnRadarOf(PlayerId player, const SimVector& position) const
    {
        // No radar grid: this is a straight range test against the player's
        // active dishes, measured in the map plane.
        for (const auto& detector : playerVisibility.at(player.value).radarDetectors)
        {
            auto dx = position.x - detector.position.x;
            auto dz = position.z - detector.position.z;
            if (((dx * dx) + (dz * dz)) <= detector.rangeSquared)
            {
                return true;
            }
        }

        return false;
    }

    bool GameSimulation::canSeeUnit(PlayerId viewer, UnitId unitId) const
    {
        // 0x465AC0 in full, in its own order. It is the original's only
        // "can this player see that unit" question: the world render's draw
        // list is built through it, and so is the enemy list the weapon scan
        // and the computer player both choose their targets out of.
        const auto& unit = getUnitState(unitId);

        // 0x465ACE: own units pass before anything else is looked at.
        if (unit.isOwnedBy(viewer))
        {
            return true;
        }

        // 0x465AE8: cloak is rejected next, however well lit the ground under
        // the unit is.
        if (unit.cloaked)
        {
            return false;
        }

        // 0x465B38. Sonar is a veto lifted, not a contact granted. With the
        // sonar bit clear, a unit whose model does not break the surface is
        // refused at 0x465B50 without the fog grid being consulted at all;
        // with it set, the unit simply carries on to the same line-of-sight
        // test everything else faces. That is why every sub-hunter in the
        // shipped data carries sonar reaching at least as far as it can shoot,
        // and why a sonar station's 1180 buys a picture rather than a target.
        //
        // "Below the surface" is measured to the top of the model, the same
        // y + def+0x16E the detection visitor uses to decide that a unit is a
        // radar contact rather than a sonar one, so the two can never disagree
        // about which side of the waterline a unit is on.
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);
        if (unit.position.y + modelHeightOf(unitDefinition) < terrain.getSeaLevel())
        {
            const auto& heard = playerVisibility.at(viewer.value).sonarContacts;
            if (heard.find(unitId) == heard.end())
            {
                return false;
            }
        }

        // 0x465B6A onwards: the fog grid, asked of each corner of the unit's
        // footprint in turn. RWE asks it once, of the unit's position.
        return isVisibleTo(viewer, unit.position);
    }

    bool GameSimulation::canDetectUnit(PlayerId viewer, UnitId unitId) const
    {
        // Everything the player's picture shows, which is a wider thing than
        // anything the simulation is allowed to act on: what can be seen, plus
        // the raw radar and sonar contacts that put a dot on the minimap at
        // 0x466DC0 and nowhere else.
        //
        // Nothing in the simulation may ask this. The original's weapon scan
        // and computer player both consult a list built through 0x465AC0,
        // which is canSeeUnit above; a radar contact is a blip and not a
        // target, and the radar picture is recomputed for one player a tick in
        // any case (section 18), so it could not feed a deterministic decision
        // even if the original wanted it to.
        if (canSeeUnit(viewer, unitId))
        {
            return true;
        }

        const auto& unit = getUnitState(unitId);
        if (unit.cloaked)
        {
            return false;
        }

        const auto& visibility = playerVisibility.at(viewer.value);
        return visibility.radarContacts.find(unitId) != visibility.radarContacts.end()
            || visibility.sonarContacts.find(unitId) != visibility.sonarContacts.end();
    }

    const UnitSpatialIndex& GameSimulation::getUnitSpatialIndex()
    {
        if (unitSpatialIndexStamp == gameTime)
        {
            return unitSpatialIndex;
        }

        // The positions recorded here are read again later in the same tick,
        // by which time the units carrying them have moved. Every query is
        // therefore widened by what a unit could have covered in the meantime
        // -- the fastest thing in the data, four times over, and never less
        // than a whole cell. The margin only costs a slightly wider sweep of
        // a packed array; getting it wrong would cost a missed target, which
        // is a behaviour change, so it is deliberately generous.
        //
        // The maximum is taken over every definition rather than over the
        // units in play because the definitions do not change after loading,
        // so it is worked out once and the per-tick rebuild just uses it.
        if (!maxUnitSpeedPerTick)
        {
            auto fastest = 0.0f;
            for (const auto& [name, definition] : unitDefinitions)
            {
                fastest = std::max(fastest, simScalarToFloat(definition.maxVelocity));
            }
            maxUnitSpeedPerTick = fastest;
        }

        auto margin = std::max(UnitSpatialIndex::CellSize, *maxUnitSpeedPerTick * 4.0f);

        unitSpatialIndex.reset(
            simScalarToFloat(terrain.leftInWorldUnits()),
            simScalarToFloat(terrain.topInWorldUnits()),
            simScalarToFloat(terrain.getWidthInWorldUnits()),
            simScalarToFloat(terrain.getHeightInWorldUnits()),
            margin);

        for (const auto& [unitId, unit] : units)
        {
            unitSpatialIndex.insert(unitId, unit.owner, simScalarToFloat(unit.position.x), simScalarToFloat(unit.position.z));
        }

        unitSpatialIndex.build();
        unitSpatialIndexStamp = gameTime;
        return unitSpatialIndex;
    }

    void GameSimulation::invalidateUnitSpatialIndex()
    {
        unitSpatialIndexStamp = std::nullopt;
    }

    bool GameSimulation::weaponCanHitUnit(const WeaponDefinition& weaponDefinition, const UnitState& attacker, const UnitState& target) const
    {
        // 0x49ABB0, asked of every candidate before range comes into it. The
        // two branches are exclusive: a water weapon is judged entirely on
        // where its target is floating, and everything else has to get both
        // ends of the shot out of the water before the air rule is so much as
        // looked at.
        auto seaLevel = terrain.getSeaLevel();
        const auto& targetDefinition = unitDefinitions.at(target.unitType);

        if (weaponDefinition.waterWeapon)
        {
            // 0x49ABF9: a floater is exempt, so a torpedo still reaches a ship
            // riding on the surface.
            if (!targetDefinition.floater && target.position.y > seaLevel)
            {
                return false;
            }

            // 0x49AC20: a hovercraft sits on the water rather than in it, and
            // half its model height standing proud of the surface is what puts
            // it out of a torpedo's reach.
            if (targetDefinition.canHover && target.position.y + (modelHeightOf(targetDefinition) / 2_ss) > seaLevel)
            {
                return false;
            }

            return true;
        }

        // 0x49ACC3 and 0x49ACEA. "Out of the water" is measured to the top of
        // the model rather than to its origin, which is what makes a submerged
        // submarine unshootable by anything that is not a torpedo, and a
        // surfaced one shootable by everything. The shooter has to be up there
        // too: a submarine's deck gun does not fire from underneath.
        const auto& attackerDefinition = unitDefinitions.at(attacker.unitType);
        if (attacker.position.y + modelHeightOf(attackerDefinition) <= seaLevel)
        {
            return false;
        }

        if (target.position.y + modelHeightOf(targetDefinition) <= seaLevel)
        {
            return false;
        }

        // 0x49AD07, and it only points this way round. The original has no
        // rule anywhere that refuses an ordinary weapon an airborne target;
        // what holds a Peewee back is wpri_badTargetCategory, which is a
        // preference and lives in the choice rather than here.
        if (weaponDefinition.toAirWeapon && !isFlying(target.physics))
        {
            return false;
        }

        // The fourth refusal, and the one that actually keeps a ship's guns
        // off aircraft: a ballistic weapon that cannot find an arc to the
        // target does not take it (0x49ABB0 calling the solver at 0x49A890).
        // Nothing else would stop it -- the Crusader names no
        // wpri_badTargetCategory, and that is a preference in the chooser
        // rather than a rule here anyway. A 300-velocity shell simply cannot
        // reach a cruising aircraft, and the arithmetic says so.
        if (std::holds_alternative<ProjectilePhysicsTypeBallistic>(weaponDefinition.physicsType))
        {
            auto aim = target.position - attacker.position;
            SimVector aimXZ(aim.x, 0_ss, aim.z);
            auto gravity = 112_ss / (30_ss * 30_ss);
            if (!computeFiringAngles(weaponDefinition.velocity, gravity, aimXZ.length(), aim.y))
            {
                return false;
            }
        }

        return true;
    }

    void GameSimulation::returnFire(UnitId victimId, UnitId attackerId)
    {
        if (victimId == attackerId)
        {
            return;
        }

        auto attackerRef = tryGetUnitState(attackerId);
        if (!attackerRef)
        {
            return;
        }
        const auto& attacker = attackerRef->get();
        if (attacker.isDead())
        {
            return;
        }

        auto& victim = getUnitState(victimId);
        if (victim.isDead() || victim.fireOrders == UnitFireOrders::HoldFire || victim.isOwnedBy(attacker.owner))
        {
            return;
        }

        const auto& victimDefinition = unitDefinitions.at(victim.unitType);
        if (victim.isBeingBuilt(victimDefinition))
        {
            return;
        }

        for (unsigned int i = 0; i < victim.weapons.size(); ++i)
        {
            const auto& weapon = victim.weapons[i];
            if (!weapon || !std::holds_alternative<UnitWeaponStateIdle>(weapon->state))
            {
                continue;
            }

            const auto& weaponDefinition = weaponDefinitions.at(weapon->weaponType);
            if (weaponDefinition.commandFire)
            {
                continue;
            }

            if (victim.position.distanceSquared(attacker.position) > weaponDefinition.maxRange * weaponDefinition.maxRange)
            {
                continue;
            }

            if (!weaponCanHitUnit(weaponDefinition, victim, attacker))
            {
                continue;
            }

            victim.setWeaponTarget(i, attackerId);
        }
    }

    SimScalar GameSimulation::modelHeightOf(const UnitDefinition& unitDefinition) const
    {
        if (auto model = unitModelDefinitions.find(unitDefinition.objectName); model != unitModelDefinitions.end())
        {
            return model->second.height;
        }

        return 0_ss;
    }

    void GameSimulation::updateVisibility()
    {
        for (auto& v : playerVisibility)
        {
            v.clearCurrent();
        }

        // World units per vision cell; sight and radar ranges are in world units.
        auto cellWorldUnits = static_cast<int>(simScalarToUInt(MapTerrain::HeightTileWidthInWorldUnits)) * PlayerVisibility::VisionCellSizeInTiles;
        auto seaLevel = static_cast<int>(std::min(simScalarToUInt(terrain.getSeaLevel()), 255u));

        for (const auto& [unitId, unit] : units)
        {
            if (unit.isDead())
            {
                continue;
            }
            const auto& unitDefinition = unitDefinitions.at(unit.unitType);

            // The eye sits at the top of the unit's model, never below the
            // water's surface, and the whole thing lives in the heightmap's
            // 0..255 range.
            auto modelHeight = static_cast<int>(std::floor(modelHeightOf(unitDefinition).value));
            auto groundLevel = std::max(static_cast<int>(std::floor(unit.position.y.value)), seaLevel + 1);
            auto eyeHeight = std::clamp(groundLevel + modelHeight, 0, 255);

            // Circular sight does not walk the ray tables at all, so their cap
            // of 8 cells does not apply to it; it saturates at the largest of
            // the original's mask sprites instead. Terrain-mode sight is
            // capped because TA indexes its tables with
            // min(SightDistance / 32, numtables - 1).
            auto circular = lineOfSightMode == LineOfSightMode::Circular;
            auto radius = std::min(
                static_cast<int>(unitDefinition.sightDistance) / cellWorldUnits,
                circular ? MaxCircularSightRadiusInCells : losTables.maxRadius());

            auto cell = visionCellAt(unit.position);

            // The unit's bit in the one shared explored grid. Every player in
            // the owner's line-of-sight group owns that same bit, so revealing
            // for the owner and each ally writes it more than once for the same
            // cell -- harmlessly, since setting a bit twice sets it once -- and
            // the grounds are shared without a rival grid per player.
            auto exploredMark = ExploredMark{&explored, losGroupBitFor(unit.owner)};

            auto revealFor = [&](PlayerVisibility& vis) {
                if (circular)
                {
                    vis.revealCircle(cell, radius, exploredMark);
                }
                else
                {
                    vis.revealWithLineOfSight(cell, radius, visionHeights, eyeHeight, losTables, exploredMark);
                }
            };

            // The owner always sees through its own units; allies see too,
            // so a teammate's map is lit by your scouts and yours by theirs.
            // The owner's own reveal is written plainly rather than left to
            // fall out of the alliance test -- a unit's own player seeing its
            // own surroundings is not a thing to make conditional.
            revealFor(playerVisibility.at(unit.owner.value));
            for (std::size_t i = 0; i < playerVisibility.size() && i < players.size(); ++i)
            {
                if (i == unit.owner.value || !arePlayersAllied(unit.owner, PlayerId(static_cast<unsigned int>(i))))
                {
                    continue;
                }
                revealFor(playerVisibility[i]);
            }

            // Radar, sonar and jamming all need the unit switched on if it can
            // be switched at all. Altitude extends radar; sonar is flat.
            auto detectorActive = (!unitDefinition.onOffable || unit.activated) && !unit.isBeingBuilt(unitDefinition);
            if (!detectorActive)
            {
                continue;
            }

            auto altitude = rweMax(unit.position.y, 0_ss);
            for (std::size_t i = 0; i < playerVisibility.size() && i < players.size(); ++i)
            {
                if (!arePlayersAllied(unit.owner, PlayerId(static_cast<unsigned int>(i))))
                {
                    continue;
                }
                auto& alliedVis = playerVisibility[i];
                if (unitDefinition.radarDistance > 0)
                {
                    // Altitude extends radar, and then an outer cap takes it
                    // straight back off again. The visitor tests against
                    // `RadarDistance + 2 x floor(detector Y)` (0x467932), but
                    // the sweep only offers it units inside
                    // `max(RadarDistance, SonarDistance)` (0x4675A3 into
                    // 0x47E9E4), so the effective reach is the smaller.
                    //
                    // On the shipped data the cap always wins: no unit has
                    // more sonar than radar while having any radar. So a
                    // Peeper reaches exactly its RadarDistance, which is what
                    // its minimap ring draws -- and RWE, having the bonus
                    // without the cap, was detecting half as far again as it
                    // drew.
                    auto declared = intToSimScalar(static_cast<int>(unitDefinition.radarDistance));
                    auto lifted = declared + (2_ss * altitude);
                    auto cap = rweMax(declared, intToSimScalar(static_cast<int>(unitDefinition.sonarDistance)));
                    auto range = rweMin(lifted, cap);
                    alliedVis.radarDetectors.push_back(PlayerVisibility::RadarDetector{unit.position, range * range, false});
                }
                if (unitDefinition.sonarDistance > 0)
                {
                    auto range = intToSimScalar(static_cast<int>(unitDefinition.sonarDistance));
                    alliedVis.radarDetectors.push_back(PlayerVisibility::RadarDetector{unit.position, range * range, true});
                }
            }

            // A jammer works against everyone but its own owner. The original
            // builds this list while walking the same unit array, skipping only
            // the units of the player whose picture it is drawing, so an ally's
            // jammer blanks your radar exactly as an enemy's does.
            for (std::size_t i = 0; i < playerVisibility.size(); ++i)
            {
                if (i == unit.owner.value)
                {
                    continue;
                }

                auto& theirVis = playerVisibility[i];
                if (unitDefinition.radarDistanceJam > 0)
                {
                    auto range = intToSimScalar(static_cast<int>(unitDefinition.radarDistanceJam));
                    theirVis.radarJammers.push_back(PlayerVisibility::RadarJammer{unit.position, range * range, false});
                }
                if (unitDefinition.sonarDistanceJam > 0)
                {
                    auto range = intToSimScalar(static_cast<int>(unitDefinition.sonarDistanceJam));
                    theirVis.radarJammers.push_back(PlayerVisibility::RadarJammer{unit.position, range * range, true});
                }
            }
        }

        // Permanent sight, once every unit has had its say: ground that has
        // been seen never dims back to memory. Promoting the explored grid
        // into the visible one is the whole of the option -- the units
        // standing on remembered ground come back with the ground, because
        // canSeeUnit asks about the ground and not about live vision. With
        // Mapped alongside it, the explored grid was already full before the
        // first tick, so the whole map is lit from the start.
        if (lineOfSightMode == LineOfSightMode::Permanent)
        {
            for (std::size_t i = 0; i < playerVisibility.size(); ++i)
            {
                playerVisibility[i].makeExploredVisible(ExploredMark{&explored, losGroupBitFor(PlayerId(static_cast<unsigned int>(i)))});
            }
        }

        // Radar is a unit-versus-unit range query, not a grid: terrain never
        // blocks it and it reveals no ground, it only flags contacts.
        auto seaLevelScalar = terrain.getSeaLevel();
        for (std::size_t i = 0; i < playerVisibility.size(); ++i)
        {
            auto& vis = playerVisibility[i];
            if (vis.radarDetectors.empty())
            {
                continue;
            }

            PlayerId player(static_cast<unsigned int>(i));
            for (const auto& [unitId, unit] : units)
            {
                if (unit.isDead() || unit.isOwnedBy(player))
                {
                    continue;
                }

                // Stealth is absolute: the original's detection visitor drops
                // the unit before it measures anything.
                const auto& targetDefinition = unitDefinitions.at(unit.unitType);
                if (targetDefinition.stealth)
                {
                    continue;
                }

                // Which of the two contacts a unit can be is decided by where
                // it sits relative to the waterline: sonar finds anything at or
                // below it, radar anything whose model rises above it. A
                // half-submerged unit is both, and a unit on dry land at sea
                // level is too.
                auto modelTop = unit.position.y + modelHeightOf(targetDefinition);
                auto byRadar = modelTop >= seaLevelScalar && isInRangeOfAny(vis.radarDetectors, unit.position, false);
                auto bySonar = unit.position.y <= seaLevelScalar && isInRangeOfAny(vis.radarDetectors, unit.position, true);

                // Jamming runs after detection and wins over it, and it only
                // touches the contact it is aimed at: a radar jammer says
                // nothing about what sonar can hear.
                if (byRadar && isInRangeOfAny(vis.radarJammers, unit.position, false))
                {
                    byRadar = false;
                }
                if (bySonar && isInRangeOfAny(vis.radarJammers, unit.position, true))
                {
                    bySonar = false;
                }

                // Kept apart rather than merged. The original sets two bits
                // and reads them in two places for two purposes: bit 8 is the
                // picture, bit 9 is what 0x465AC0 consults before it will
                // admit anything below the waterline.
                if (byRadar)
                {
                    vis.radarContacts.insert(unitId);
                }
                if (bySonar)
                {
                    vis.sonarContacts.insert(unitId);
                }
            }
        }
    }

    void GameSimulation::updateCloakSuppression()
    {
        // The original runs this in the same per-tick pass as the radar
        // picture: every cloakable unit asks whether a live enemy is standing
        // within MinCloakDistance of it, and if one is, its cloak is held off
        // for the next ninety ticks. The flag is a timestamp rather than a
        // latch, so the unit stays visible for three seconds after the enemy
        // walks away instead of blinking back the moment it is out of range.
        for (auto& entry : units)
        {
            auto& unit = entry.second;
            if (unit.isDead())
            {
                continue;
            }

            const auto& unitDefinition = unitDefinitions.at(unit.unitType);
            if (!unitDefinition.cloakable)
            {
                continue;
            }

            auto minDistance = intToSimScalar(static_cast<int>(unitDefinition.minCloakDistance));
            auto minDistanceSquared = minDistance * minDistance;

            for (const auto& otherEntry : units)
            {
                const auto& otherUnit = otherEntry.second;
                if (otherUnit.isDead() || otherUnit.isOwnedBy(unit.owner))
                {
                    continue;
                }

                auto dx = otherUnit.position.x - unit.position.x;
                auto dz = otherUnit.position.z - unit.position.z;
                if (((dx * dx) + (dz * dz)) <= minDistanceSquared)
                {
                    unit.cloakSuppressedUntil = gameTime + GameTime(CloakSuppressionTicks);
                    break;
                }
            }
        }
    }
}
