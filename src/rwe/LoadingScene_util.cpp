#include "LoadingScene_util.h"
#include <algorithm>
#include <cmath>

#include <rwe/util/SpanStream.h>
#include <rwe/sim/UnitState.h>

namespace rwe
{
    MovementClassCollisionService createMovementClassCollisionService(const MapTerrain& terrain, const MovementClassDatabase& movementClassDatabase)
    {
        MovementClassCollisionService service;
        for (const auto& pair : movementClassDatabase)
        {
            service.registerMovementClass(pair.first, computeWalkableGrid(terrain, pair.second));
        }
        return service;
    }


    std::unordered_map<std::string, CobScript> loadCobScripts(AbstractVirtualFileSystem& vfs)
    {
        std::unordered_map<std::string, CobScript> output;

        auto scripts = vfs.getFileNames("scripts", ".cob");

        for (const auto& scriptName : scripts)
        {
            auto bytes = vfs.readFile("scripts/" + scriptName);
            if (!bytes)
            {
                throw std::runtime_error("File in listing could not be read: " + scriptName);
            }

            rwe::SpanStream s(bytes->data(), bytes->size());
            auto cob = parseCob(s);

            auto scriptNameWithoutExtension = scriptName.substr(0, scriptName.size() - 4);

            output.insert({toUpper(scriptNameWithoutExtension), std::move(cob)});
        }

        return output;
    }

    std::vector<std::string> getFeatureNames(TntArchive& tnt)
    {
        std::vector<std::string> features;

        tnt.readFeatures([&](const auto& featureName) {
            features.push_back(featureName);
        });

        return features;
    }

    Grid<std::size_t> getMapData(TntArchive& tnt)
    {
        auto mapWidthInTiles = tnt.getHeader().width / 2;
        auto mapHeightInTiles = tnt.getHeader().height / 2;
        std::vector<uint16_t> mapData(mapWidthInTiles * mapHeightInTiles);
        tnt.readMapData(mapData.data());
        std::vector<std::size_t> dataCopy;
        dataCopy.reserve(mapData.size());
        std::copy(mapData.begin(), mapData.end(), std::back_inserter(dataCopy));
        Grid<std::size_t> dataGrid(mapWidthInTiles, mapHeightInTiles, std::move(dataCopy));
        return dataGrid;
    }

    Grid<unsigned char> getHeightGrid(const Grid<TntTileAttributes>& attrs)
    {
        const auto& sourceData = attrs.getVector();

        std::vector<unsigned char> data;
        data.reserve(sourceData.size());

        std::transform(sourceData.begin(), sourceData.end(), std::back_inserter(data), [](const TntTileAttributes& e) {
            return e.height;
        });

        return Grid<unsigned char>(attrs.getWidth(), attrs.getHeight(), std::move(data));
    }

    Vector3f colorToVector(const Color& color)
    {
        return Vector3f(
            static_cast<float>(color.r) / 255.0f,
            static_cast<float>(color.g) / 255.0f,
            static_cast<float>(color.b) / 255.0f);
    }

    unsigned int colorDistance(const Color& a, const Color& b)
    {
        auto dr = a.r > b.r ? a.r - b.r : b.r - a.r;
        auto dg = a.g > b.g ? a.g - b.g : b.g - a.g;
        auto db = a.b > b.b ? a.b - b.b : b.b - a.b;
        return dr + dg + db;
    }

    Vector3f getLaserColorUtil(const std::vector<Color>& palette, const std::vector<Color>& guiPalette, unsigned int colorIndex)
    {
        // In TA, lasers use the GUIPAL colors,
        // but these must be mapped to a color available
        // in the in-game PALETTE.
        const auto& guiColor = guiPalette.at(colorIndex);

        auto elem = std::min_element(
            palette.begin(),
            palette.end(),
            [&guiColor](const auto& a, const auto& b) { return colorDistance(guiColor, a) < colorDistance(guiColor, b); });

        return colorToVector(*elem);
    }

    std::optional<std::string> getFxName(unsigned int code)
    {
        switch (code)
        {
            case 0:
                return "cannonshell";
            case 1:
                return "plasmasm";
            case 2:
                return "plasmamd";
            case 3:
                return "ultrashell";
            case 4:
                return "plasmasm";
            default:
                return std::nullopt;
        }
    }

    MovementClassDefinition parseMovementClassDefinition(const MovementClassTdf& tdf)
    {
        MovementClassDefinition mc;

        mc.name = tdf.name;
        mc.footprintX = tdf.footprintX;
        mc.footprintZ = tdf.footprintZ;
        mc.minWaterDepth = tdf.minWaterDepth;
        mc.maxWaterDepth = tdf.maxWaterDepth;
        mc.maxSlope = tdf.maxSlope;
        mc.maxWaterSlope = tdf.maxWaterSlope;

        return mc;
    }

    WeaponDefinition parseWeaponDefinition(const WeaponTdf& tdf)
    {
        WeaponDefinition weaponDefinition;

        weaponDefinition.maxRange = SimScalar(tdf.range);
        weaponDefinition.reloadTime = SimScalar(tdf.reloadTime);
        weaponDefinition.tolerance = SimAngle(tdf.tolerance);
        weaponDefinition.pitchTolerance = SimAngle(tdf.pitchTolerance);
        weaponDefinition.turret = tdf.turret;
        weaponDefinition.verticalLaunch = tdf.vLaunch;
        weaponDefinition.velocity = SimScalar(static_cast<float>(tdf.weaponVelocity) / 30.0f);

        weaponDefinition.burst = tdf.burst;
        weaponDefinition.burstInterval = SimScalar(tdf.burstRate);
        weaponDefinition.sprayAngle = SimAngle(tdf.sprayAngle);
        weaponDefinition.accuracy = SimAngle(tdf.accuracy);

        weaponDefinition.energyPerShot = Energy(tdf.energyPerShot);
        weaponDefinition.metalPerShot = Metal(tdf.metalPerShot);
        weaponDefinition.stockpile = tdf.stockpile;

        if (tdf.selfProp)
        {
            // TA's projectile update tests `selfprop` before anything else, so a
            // missile that also carries `lineofsight=1` -- which most of them do --
            // is flown by the motor, not in a straight line (0x49B9C2). The three
            // speeds are per second in the TDF and the original converts them to
            // per tick and per tick squared as it parses; `turnrate` likewise, and
            // it truncates rather than rounds (0x42E4C6, 0x42E60E, 0x4E43A0).
            ProjectilePhysicsTypeSelfPropelled p;
            p.startVelocity = SimScalar(static_cast<float>(tdf.startVelocity) / 30.0f);
            p.acceleration = SimScalar(static_cast<float>(tdf.weaponAcceleration) / 900.0f);
            p.maxVelocity = SimScalar(static_cast<float>(tdf.weaponVelocity) / 30.0f);
            p.turnRate = SimAngle(static_cast<uint16_t>(tdf.turnRate / 30u));
            p.guidance = tdf.guidance;
            p.tracks = tdf.tracks;
            p.twoPhase = tdf.twoPhase;
            p.vLaunch = tdf.vLaunch;
            p.flightTime = GameTime(static_cast<unsigned int>(tdf.flightTime * 30.0f));
            p.burnBlow = tdf.burnBlow;
            p.cruise = tdf.cruise;
            p.autoRange = !tdf.noAutoRange && tdf.weaponVelocity != 0;
            weaponDefinition.physicsType = p;
        }
        else if (tdf.tracks)
        {
            weaponDefinition.physicsType = ProjectilePhysicsTypeTracking{SimScalar(tdf.turnRate)};
        }
        else if (tdf.lineOfSight)
        {
            weaponDefinition.physicsType = ProjectilePhysicsTypeLineOfSight();
        }
        else if (tdf.dropped)
        {
            // Bombs in TA set both `ballistic=1` and `dropped=1`. The `dropped`
            // flag identifies bombsight semantics (release-when-overhead,
            // inherit aircraft velocity), and supersedes the ballistic
            // aim-then-launch flow.
            weaponDefinition.physicsType = ProjectilePhysicsTypeBomb();
        }
        else if (tdf.ballistic)
        {
            weaponDefinition.physicsType = ProjectilePhysicsTypeBallistic();
        }

        weaponDefinition.commandFire = tdf.commandFire;

        for (const auto& p : tdf.damage)
        {
            weaponDefinition.damage.insert_or_assign(toUpper(p.first), p.second);
        }

        weaponDefinition.damageRadius = SimScalar(static_cast<float>(tdf.areaOfEffect) / 2.0f);
        weaponDefinition.edgeEffectiveness = SimScalar(tdf.edgeEffectiveness);

        if (tdf.weaponTimer != 0.0f)
        {
            weaponDefinition.weaponTimer = GameTime(static_cast<unsigned int>(tdf.weaponTimer * 30.0f));
        }

        weaponDefinition.groundBounce = tdf.groundBounce;

        weaponDefinition.fireStarter = static_cast<unsigned int>(std::clamp(tdf.fireStarter, 0.0f, 100.0f));

        weaponDefinition.waterWeapon = tdf.waterWeapon;
        weaponDefinition.paralyzer = tdf.paralyzer;
        weaponDefinition.toAirWeapon = tdf.toAirWeapon;

        weaponDefinition.randomDecay = GameTime(static_cast<unsigned int>(tdf.randomDecay * 30.0f));

        return weaponDefinition;
    }

    std::optional<YardMapCell> parseYardMapCell(char c)
    {
        switch (c)
        {
            case 'c':
                return YardMapCell::GroundGeoPassableWhenOpen;
            case 'C':
                return YardMapCell::WaterPassableWhenOpen;
            case 'f':
                return YardMapCell::GroundNoFeature;
            case 'g':
                return YardMapCell::GroundGeoPassableWhenOpen;
            case 'G':
                return YardMapCell::Geo;
            case 'o':
                return YardMapCell::Ground;
            case 'O':
                return YardMapCell::GroundPassableWhenClosed;
            case 'w':
                return YardMapCell::Water;
            case 'y':
                return YardMapCell::GroundPassable;
            case 'Y':
                return YardMapCell::WaterPassable;
            case '.':
                return YardMapCell::Passable;
            case ' ':
                return std::nullopt;
            case '\r':
                return std::nullopt;
            case '\n':
                return std::nullopt;
            case '\t':
                return std::nullopt;
            default:
                return YardMapCell::Ground;
        }
    }

    std::vector<YardMapCell> parseYardMapCells(const std::string& yardMap)
    {
        std::vector<YardMapCell> cells;
        for (const auto& c : yardMap)
        {
            auto cell = parseYardMapCell(c);
            if (cell)
            {
                cells.push_back(*cell);
            }
        }
        return cells;
    }

    Grid<YardMapCell> parseYardMap(unsigned int width, unsigned int height, const std::string& yardMap)
    {
        auto cells = parseYardMapCells(yardMap);
        cells.resize(width * height, YardMapCell::Ground);
        return Grid<YardMapCell>(width, height, std::move(cells));
    }

    /**
     * The original stores both standing orders as two-bit fields and reads
     * them back with a plain mask, so a value it does not recognise wraps
     * rather than being rejected. Nothing in the shipped data relies on that,
     * but a third-party unit naming 3 should land somewhere defined instead of
     * off the end of the enum.
     */
    UnitMovementOrders parseStandingMoveOrder(unsigned int value)
    {
        switch (value & 3u)
        {
            case 0:
                return UnitMovementOrders::HoldPosition;
            case 1:
                return UnitMovementOrders::Maneuver;
            default:
                return UnitMovementOrders::Roam;
        }
    }

    UnitFireOrders parseStandingFireOrder(unsigned int value)
    {
        switch (value & 3u)
        {
            case 0:
                return UnitFireOrders::HoldFire;
            case 1:
                return UnitFireOrders::ReturnFire;
            default:
                return UnitFireOrders::FireAtWill;
        }
    }

    UnitDefinition parseUnitDefinition(const UnitFbi& fbi, MovementClassDatabase& movementClassDatabase)
    {
        UnitDefinition u;

        u.unitName = fbi.name;
        u.unitDescription = fbi.description;
        u.objectName = fbi.objectName;

        u.turnRate = SimScalar(fbi.turnRate);
        u.maxVelocity = SimScalar(fbi.maxVelocity);

        // A missing MoveRate is twice the unit's top speed in the original, so
        // the threshold is one the unit can never cross and everything that
        // moves is in the first band.
        u.moveRate1 = fbi.moveRate1 > 0.0f ? SimScalar(fbi.moveRate1) : SimScalar(fbi.maxVelocity) * 2_ss;
        u.moveRate2 = fbi.moveRate2 > 0.0f ? SimScalar(fbi.moveRate2) : SimScalar(fbi.maxVelocity) * 2_ss;
        u.acceleration = SimScalar(fbi.acceleration);
        u.brakeRate = SimScalar(fbi.brakeRate);

        u.canAttack = fbi.canAttack;
        u.canMove = fbi.canMove;
        u.canGuard = fbi.canGuard;
        u.canCapture = fbi.canCapture;
        u.canDgun = fbi.canDgun;
        // The original has no Cloakable key: it derives the flag from CloakCost
        // being greater than zero, and nothing in the shipped data writes the
        // key. Honour both so a mod that only says Cloakable still works.
        u.cloakable = fbi.cloakable || fbi.cloakCost > 0.0f;

        u.mobileStandOrders = fbi.mobileStandOrders;
        u.fireStandOrders = fbi.fireStandOrders;
        u.standingMoveOrder = parseStandingMoveOrder(fbi.standingMoveOrder);
        u.standingFireOrder = parseStandingFireOrder(fbi.standingFireOrder);

        u.commander = fbi.commander;

        u.maxHitPoints = fbi.maxDamage;
        u.damageModifier = simScalarToFixed(SimScalar(fbi.damageModifier));

        u.isMobile = fbi.bmCode;

        u.floater = fbi.floater;
        u.canHover = fbi.canHover;

        u.canFly = fbi.canFly;
        u.transportCapacity = fbi.transportCapacity;
        u.transportSize = fbi.transportSize;
        if (u.transportSize > 0 && u.transportCapacity == 0)
        {
            // TA gives no capacity in the FBI: air transports carry one unit,
            // the Hulk and Envoy six (the crane loads units onto the deck).
            u.transportCapacity = fbi.floater ? 6u : 1u;
        }

        u.cruiseAltitude = SimScalar(fbi.cruiseAlt);
        u.bankScale = SimScalar(fbi.bankScale);
        u.hoverAttack = fbi.hoverAttack;
        u.attackRunLength = SimScalar(static_cast<float>(fbi.attackRunLength));
        u.maneuverLeashLength = SimScalar(static_cast<float>(fbi.maneuverLeashLength));

        u.weapon1 = fbi.weapon1;
        u.weapon2 = fbi.weapon2;
        u.weapon3 = fbi.weapon3;

        u.explodeAs = fbi.explodeAs;
        u.selfDestructAs = fbi.selfDestructAs;

        u.builder = fbi.builder;
        u.buildTime = fbi.buildTime;
        u.buildCostEnergy = Energy(fbi.buildCostEnergy);
        u.buildCostMetal = Metal(fbi.buildCostMetal);

        u.workerTimePerTick = fbi.workerTime / 30;

        u.buildDistance = SimScalar(fbi.buildDistance);
        u.buildAngle = SimAngle(static_cast<uint16_t>(fbi.buildAngle));

        u.onOffable = fbi.onOffable;
        u.activateWhenBuilt = fbi.activateWhenBuilt;

        u.energyMake = Energy(fbi.energyMake);
        u.metalMake = Metal(fbi.metalMake);
        u.energyUse = Energy(fbi.energyUse);
        u.metalUse = Metal(fbi.metalUse);

        u.makesMetal = Metal(fbi.makesMetal);
        u.extractsMetal = Metal(fbi.extractsMetal);

        u.energyStorage = Energy(fbi.energyStorage);
        u.metalStorage = Metal(fbi.metalStorage);

        u.windGenerator = Energy(fbi.windGenerator);
        u.tidalGenerator = Energy(fbi.tidalGenerator);

        u.corpse = fbi.corpse;

        u.hideDamage = fbi.hideDamage;
        u.showPlayerName = fbi.showPlayerName;

        u.soundCategory = fbi.soundCategory;

        // AI classification metadata. Stored on the UnitDefinition so
        // future Phase 2 work (UnitClassifier, fog-of-war) can read them
        // without re-parsing FBIs.
        u.tedClass = fbi.tedClass;
        u.category = fbi.category;
        u.badTargetCategory = {fbi.wpriBadTargetCategory, fbi.wsecBadTargetCategory, fbi.wspeBadTargetCategory};
        u.noChaseCategory = fbi.noChaseCategory;
        u.shootMe = fbi.shootMe;
        u.kamikaze = fbi.kamikaze;
        u.kamikazeDistance = fbi.kamikazeDistance;
        u.immuneToParalyzer = fbi.immuneToParalyzer;
        u.sightDistance = fbi.sightDistance;
        u.radarDistance = fbi.radarDistance;
        u.sonarDistance = fbi.sonarDistance;
        u.radarDistanceJam = fbi.radarDistanceJam;
        u.sonarDistanceJam = fbi.sonarDistanceJam;
        u.stealth = fbi.stealth;

        // The original truncates both cloak costs to whole numbers as it parses
        // them, so it never spends a fraction of an energy.
        u.cloakCost = Energy(std::trunc(fbi.cloakCost));
        u.cloakCostMoving = Energy(std::trunc(fbi.cloakCostMoving));
        u.minCloakDistance = fbi.minCloakDistance;
        u.initCloaked = fbi.initCloaked;

        u.yardMapContainsGeo = false;

        auto movementClassId = movementClassDatabase.resolveMovementClassByName(fbi.movementClass);

        if (movementClassId)
        {
            u.movementCollisionInfo = UnitDefinition::NamedMovementClass{*movementClassId};
        }
        else
        {
            u.movementCollisionInfo = UnitDefinition::AdHocMovementClass{
                fbi.footprintX,
                fbi.footprintZ,
                fbi.maxSlope,
                fbi.maxWaterSlope,
                fbi.minWaterDepth,
                fbi.maxWaterDepth,
            };

            u.yardMap = parseYardMap(fbi.footprintX, fbi.footprintZ, fbi.yardMap);
            if (u.yardMap->any(isWater))
            {
                u.floater = true;
            }
            if (u.yardMap->any(isGeo))
            {
                u.yardMapContainsGeo = true;
            }
        }

        return u;
    }

    WeaponMediaInfo parseWeaponMediaInfo(const std::vector<Color>& palette, const std::vector<Color>& guiPalette, const WeaponTdf& tdf)
    {
        WeaponMediaInfo mediaInfo;

        mediaInfo.soundTrigger = tdf.soundTrigger;

        if (!tdf.soundStart.empty())
        {
            mediaInfo.soundStart = tdf.soundStart;
        }
        if (!tdf.soundHit.empty())
        {
            mediaInfo.soundHit = tdf.soundHit;
        }
        if (!tdf.soundWater.empty())
        {
            mediaInfo.soundWater = tdf.soundWater;
        }

        mediaInfo.startSmoke = tdf.startSmoke;
        mediaInfo.endSmoke = tdf.endSmoke;
        if (tdf.smokeTrail)
        {
            mediaInfo.smokeTrail = GameTime(static_cast<unsigned int>(tdf.smokeDelay * 30.0f));
        }

        switch (tdf.renderType)
        {
            case 0:
            {
                mediaInfo.renderType = ProjectileRenderTypeLaser{
                    getLaserColorUtil(palette, guiPalette, tdf.color),
                    getLaserColorUtil(palette, guiPalette, tdf.color2),
                    SimScalar(tdf.duration * 30.0f * 2.0f), // duration seems to match better if doubled
                };
                break;
            }
            case 1:
            {
                if (tdf.model.empty())
                {
                    mediaInfo.renderType = ProjectileRenderTypeNone{};
                }
                else
                {
                    mediaInfo.renderType = ProjectileRenderTypeModel{
                        toUpper(tdf.model), ProjectileRenderTypeModel::RotationMode::HalfZ};
                }
                break;
            }
            case 3:
            {
                if (tdf.model.empty())
                {
                    mediaInfo.renderType = ProjectileRenderTypeNone{};
                }
                else
                {
                    mediaInfo.renderType = ProjectileRenderTypeModel{
                        toUpper(tdf.model), ProjectileRenderTypeModel::RotationMode::QuarterY};
                }
                break;
            }
            case 4:
            {
                auto fxName = getFxName(tdf.color);
                if (fxName)
                {

                    mediaInfo.renderType = ProjectileRenderTypeSprite{"fx", *fxName};
                }
                else
                {
                    mediaInfo.renderType = ProjectileRenderTypeNone{};
                }

                break;
            }
            case 5:
            {
                mediaInfo.renderType = ProjectileRenderTypeFlamethrower{};
                break;
            }
            case 6:
            {
                if (tdf.model.empty())
                {
                    mediaInfo.renderType = ProjectileRenderTypeNone{};
                }
                else
                {
                    mediaInfo.renderType = ProjectileRenderTypeModel{
                        toUpper(tdf.model), ProjectileRenderTypeModel::RotationMode::None};
                }
                break;
            }
            case 7:
            {
                mediaInfo.renderType = ProjectileRenderTypeLightning{
                    getLaserColorUtil(palette, guiPalette, tdf.color),
                    SimScalar(tdf.duration * 30.0f * 2.0f), // duration seems to match better if doubled
                };
                break;
            }
            default:
            {
                mediaInfo.renderType = ProjectileRenderTypeLaser{
                    Vector3f(0.0f, 0.0f, 0.0f),
                    Vector3f(0.0f, 0.0f, 0.0f),
                    SimScalar(4.0f)};
                break;
            }
        }

        if (!tdf.explosionGaf.empty() && !tdf.explosionArt.empty())
        {
            mediaInfo.explosionAnim = AnimLocation{tdf.explosionGaf, tdf.explosionArt};
        }

        if (!tdf.waterExplosionGaf.empty() && !tdf.waterExplosionArt.empty())
        {
            mediaInfo.waterExplosionAnim = AnimLocation{tdf.waterExplosionGaf, tdf.waterExplosionArt};
        }

        return mediaInfo;
    }

    FeatureDefinitionId getFeatureId(FeatureDefinitionId& nextId, const std::unordered_map<std::string, FeatureDefinitionId>& featureNameIndex, std::deque<std::string>& openQueue, std::unordered_map<std::string, FeatureDefinitionId>& openSet, const std::string& featureName)
    {
        if (auto existingId = featureNameIndex.find(featureName); existingId != featureNameIndex.end())
        {
            return existingId->second;
        }

        if (auto it = openSet.find(toUpper(featureName)); it != openSet.end())
        {
            return it->second;
        }

        auto id = nextId;
        openQueue.push_back(featureName);
        openSet.insert({toUpper(featureName), nextId});
        nextId = FeatureDefinitionId(nextId.value + 1);
        return id;
    }
}
