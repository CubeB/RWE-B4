#include "GameSimulationLoader.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <filesystem>
#include <iterator>
#include <limits>
#include <random>
#include <set>
#include <rwe/LoadingScene_util.h>
#include <rwe/sim/MissionScripts.h>
#include <rwe/ai/AiPersonality.h>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/game/DownloadMenus.h>
#include <rwe/game/FeatureMediaInfo.h>
#include <rwe/game/featureplacement.h>
#include <rwe/geometry/CollisionMesh.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/featuretdf/io.h>
#include <rwe/io/gui/gui.h>
#include <rwe/io/lostdf/io.h>
#include <rwe/io/moveinfotdf/io.h>
#include <rwe/io/soundtdf/SoundClass.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/io/weapontdf/WeaponTdf.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    namespace
    {
        std::seed_seq seedFromGameParameters(const GameParameters& params)
        {
            std::vector<unsigned int> initialVec;
            if (params.randomSeed)
            {
                initialVec.push_back(*params.randomSeed);
            }
            std::copy(params.mapName.begin(), params.mapName.end(), std::back_inserter(initialVec));

            for (const auto& e : params.players)
            {
                if (!e)
                {
                    initialVec.push_back(0);
                    continue;
                }

                initialVec.push_back(e->color.value);
                initialVec.push_back(e->energy.value);
                initialVec.push_back(e->metal.value);
                if (e->name)
                {
                    std::copy(e->name->begin(), e->name->end(), std::back_inserter(initialVec));
                }
                else
                {
                    initialVec.push_back(0);
                }
            }

            return std::seed_seq(initialVec.begin(), initialVec.end());
        }

        void preloadSound(const GameLoadServices& services, GameMediaDatabase& meshDb, const std::string& soundName)
        {
            if (services.audioService == nullptr)
            {
                return;
            }

            auto sound = services.audioService->loadSound(soundName);
            if (!sound)
            {
                return; // sometimes sound categories name invalid sounds
            }

            meshDb.addSound(soundName, *sound);
        }

        void preloadSound(const GameLoadServices& services, GameMediaDatabase& meshDb, const std::optional<std::string>& soundName)
        {
            if (!soundName)
            {
                return;
            }

            preloadSound(services, meshDb, *soundName);
        }

        void loadFeatureMedia(
            const GameLoadServices& services,
            GameMediaDatabase& gameMediaDatabase,
            std::unordered_map<std::string, UnitModelDefinition>& modelDefinitions,
            const FeatureTdf& tdf)
        {
            FeatureMediaInfo f;

            f.world = tdf.world;
            f.description = tdf.description;
            f.category = tdf.category;

            if (!tdf.object.empty())
            {
                auto normalizedObjectName = toUpper(tdf.object);

                if (modelDefinitions.find(normalizedObjectName) == modelDefinitions.end())
                {
                    auto meshInfo = services.meshService->loadProjectileMesh(normalizedObjectName);
                    modelDefinitions.insert({normalizedObjectName, std::move(meshInfo.modelDefinition)});
                    for (const auto& m : meshInfo.pieceMeshes)
                    {
                        gameMediaDatabase.addUnitPieceMesh(normalizedObjectName, m.first, m.second);
                    }
                }
                f.renderInfo = FeatureObjectInfo{normalizedObjectName};
            }
            else
            {
                FeatureSpriteInfo spriteInfo;
                spriteInfo.transparentAnimation = tdf.animTrans;
                spriteInfo.transparentShadow = tdf.shadTrans;
                if (services.textureService != nullptr)
                {
                    if (!tdf.fileName.empty() && !tdf.seqName.empty())
                    {
                        spriteInfo.animation = services.textureService->getGafEntry("anims/" + tdf.fileName + ".GAF", tdf.seqName);
                    }
                    if (!spriteInfo.animation)
                    {
                        spriteInfo.animation = services.textureService->getDefaultSpriteSeries();
                    }

                    if (!tdf.fileName.empty() && !tdf.seqNameShad.empty())
                    {
                        // Some third-party features have broken shadow anim names (e.g. "empty"),
                        // ignore them if they don't exist.
                        spriteInfo.shadowAnimation = services.textureService->tryGetGafEntry("anims/" + tdf.fileName + ".GAF", tdf.seqNameShad);
                    }
                    if (!tdf.fileName.empty() && !tdf.seqNameBurn.empty())
                    {
                        spriteInfo.burnAnimation = services.textureService->tryGetGafEntry("anims/" + tdf.fileName + ".GAF", tdf.seqNameBurn);
                    }
                }
                f.renderInfo = std::move(spriteInfo);
            }

            f.fileName = tdf.fileName;

            // The reclaim sequence plays as a one-off particle where the feature stood.
            if (services.textureService != nullptr && !tdf.fileName.empty() && !tdf.seqNameReclamate.empty())
            {
                if (auto anim = services.textureService->tryGetGafEntry("anims/" + tdf.fileName + ".GAF", tdf.seqNameReclamate))
                {
                    gameMediaDatabase.addSpriteSeries(tdf.fileName, tdf.seqNameReclamate, *anim);
                }
            }

            f.seqNameReclamate = tdf.seqNameReclamate;

            f.seqNameBurn = tdf.seqNameBurn;
            f.seqNameBurnShad = tdf.seqNameBurnShad;

            f.seqNameDie = tdf.seqNameDie;

            gameMediaDatabase.addFeature(std::move(f));
        }

        void loadFeature(
            const GameLoadServices& services,
            GameDataMaps& dataMaps,
            const std::unordered_map<std::string, FeatureTdf>& tdfs,
            const std::string& initialFeatureName)
        {
            auto nextId = dataMaps.featureDefinitions.getNextId();
            std::unordered_map<std::string, FeatureDefinitionId> openSet{{toUpper(initialFeatureName), nextId}};
            nextId = FeatureDefinitionId(nextId.value + 1);
            for (std::deque<std::string> featuresToLoad{{initialFeatureName}}; !featuresToLoad.empty(); featuresToLoad.pop_front())
            {
                const auto& featureName = featuresToLoad.front();

                const auto& tdf = tdfs.at(toUpper(featureName));

                FeatureDefinition f;

                f.name = featureName;

                f.footprintX = tdf.footprintX;
                f.footprintZ = tdf.footprintZ;
                f.height = SimScalar(tdf.height);

                f.reclaimable = tdf.reclaimable;
                f.autoreclaimable = tdf.autoreclaimable;
                if (!tdf.featureReclamate.empty())
                {
                    f.featureReclamate = getFeatureId(nextId, dataMaps.featureNameIndex, featuresToLoad, openSet, tdf.featureReclamate);
                }
                f.metal = tdf.metal;
                f.energy = tdf.energy;

                f.flamable = tdf.flamable;
                if (!tdf.featureBurnt.empty())
                {
                    f.featureBurnt = getFeatureId(nextId, dataMaps.featureNameIndex, featuresToLoad, openSet, tdf.featureBurnt);
                }
                f.burnMin = tdf.burnMin;
                f.burnMax = tdf.burnMax;
                f.sparkTime = tdf.sparkTime;
                f.spreadChance = tdf.spreadChance;
                f.burnWeapon = tdf.burnWeapon;

                f.geothermal = tdf.geothermal;


                f.reproduce = tdf.reproduce;
                f.reproduceArea = tdf.reproduceArea;

                f.noDisplayInfo = tdf.noDisplayInfo;

                f.permanent = tdf.permanent;

                f.blocking = tdf.blocking;

                f.indestructible = tdf.indestructible;
                f.damage = tdf.damage;
                if (!tdf.featureDead.empty())
                {
                    f.featureDead = getFeatureId(nextId, dataMaps.featureNameIndex, featuresToLoad, openSet, tdf.featureDead);
                }

                auto id = dataMaps.featureDefinitions.insert(f);
                dataMaps.featureNameIndex.insert({toUpper(featureName), id});

                loadFeatureMedia(services, dataMaps.gameMediaDatabase, dataMaps.modelDefinitions, tdf);
            }
        }

        std::optional<std::vector<std::vector<GuiEntry>>> loadBuilderGui(const GameLoadServices& services, const std::string& unitName)
        {
            std::vector<std::vector<GuiEntry>> entries;
            for (int i = 1; auto rawGui = services.vfs->readFile(services.pathMapping->guis + "/" + unitName + std::to_string(i) + ".GUI"); ++i)
            {
                auto parsedGui = parseGuiFromBytes(*rawGui);
                if (!parsedGui)
                {
                    throw std::runtime_error("Failed to parse unit builder GUI: " + unitName + std::to_string(i));
                }
                entries.push_back(std::move(*parsedGui));
            }

            if (entries.empty())
            {
                return std::nullopt;
            }

            return entries;
        }

        GameDataMaps loadGameData(const GameLoadServices& services, const std::unordered_set<std::string>& requiredFeatures)
        {
            GameDataMaps dataMaps;

            // read sound categories
            {
                auto path = services.pathMapping->gamedata + "/SOUND.TDF";
                auto bytes = services.vfs->readFile(path);
                if (!bytes)
                {
                    throw std::runtime_error("Failed to read " + path);
                }

                std::string soundString(bytes->data(), bytes->size());
                auto sounds = parseSoundTdf(parseTdfFromString(soundString));
                for (auto& s : sounds)
                {
                    const auto& c = s.second;
                    preloadSound(services, dataMaps.gameMediaDatabase, c.select1);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.unitComplete);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.activate);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.deactivate);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.ok1);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.arrived1);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.cant1);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.underAttack);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.build);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.repair);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.working);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.cloak);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.uncloak);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.capture);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.count5);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.count4);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.count3);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.count2);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.count1);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.count0);
                    preloadSound(services, dataMaps.gameMediaDatabase, c.cancelDestruct);
                    dataMaps.gameMediaDatabase.addSoundClass(s.first, std::move(s.second));
                }
            }

            // read movement classes
            {
                auto path = services.pathMapping->gamedata + "/MOVEINFO.TDF";
                auto bytes = services.vfs->readFile(path);
                if (!bytes)
                {
                    throw std::runtime_error("Failed to read " + path);
                }

                std::string movementString(bytes->data(), bytes->size());
                auto classes = parseMoveInfoTdf(parseTdfFromString(movementString));
                for (auto& c : classes)
                {
                    auto movementClassDefinition = parseMovementClassDefinition(c.second);
                    dataMaps.movementClassDatabase.registerMovementClass(movementClassDefinition);
                }
            }

            // read the line of sight ray tables
            {
                auto path = services.pathMapping->gamedata + "/LOS.TDF";
                std::optional<LosTables> tables;
                if (auto bytes = services.vfs->readFile(path); bytes)
                {
                    try
                    {
                        std::string losString(bytes->data(), bytes->size());
                        tables = parseLosTdf(parseTdfFromString(losString));
                    }
                    catch (const std::exception& e)
                    {
                        LOG_WARN << "Failed to parse " << path << ": " << e.what();
                    }
                }

                if (tables)
                {
                    dataMaps.losTables = std::move(*tables);
                }
                else
                {
                    // Not fatal: without the authored fans, generate ray fans of
                    // the same shape so a game can still be played.
                    LOG_WARN << "Could not read " << path << ", generating line of sight tables instead";
                    dataMaps.losTables = generateLosTables(DefaultLosTableCount - 1);
                }
            }

            // read weapons
            {
                auto weaponFiles = services.vfs->getFileNames(services.pathMapping->weapons, ".tdf");

                for (const auto& fileName : weaponFiles)
                {
                    auto bytes = services.vfs->readFile(services.pathMapping->weapons + "/" + fileName);
                    if (!bytes)
                    {
                        throw std::runtime_error("File in listing could not be read: " + fileName);
                    }

                    std::string tdfString(bytes->data(), bytes->size());
                    auto entries = parseWeaponTdf(parseTdfFromString(tdfString));

                    for (auto& pair : entries)
                    {
                        auto weaponDefinition = parseWeaponDefinition(pair.second);
                        auto weaponMediaInfo = parseWeaponMediaInfo(*services.palette, *services.guiPalette, pair.second);

                        preloadSound(services, dataMaps.gameMediaDatabase, weaponMediaInfo.soundStart);
                        preloadSound(services, dataMaps.gameMediaDatabase, weaponMediaInfo.soundHit);
                        preloadSound(services, dataMaps.gameMediaDatabase, weaponMediaInfo.soundWater);

                        if (auto modelRenderType = std::get_if<ProjectileRenderTypeModel>(&weaponMediaInfo.renderType); modelRenderType != nullptr)
                        {
                            auto meshInfo = services.meshService->loadProjectileMesh(modelRenderType->objectName);
                            dataMaps.modelDefinitions.insert({modelRenderType->objectName, std::move(meshInfo.modelDefinition)});
                            for (const auto& m : meshInfo.pieceMeshes)
                            {
                                dataMaps.gameMediaDatabase.addUnitPieceMesh(modelRenderType->objectName, m.first, m.second);
                            }
                        }

                        if (services.textureService != nullptr)
                        {
                            if (weaponMediaInfo.explosionAnim)
                            {
                                auto anim = services.textureService->getGafEntry("anims/" + weaponMediaInfo.explosionAnim->gafName + ".gaf", weaponMediaInfo.explosionAnim->animName);
                                dataMaps.gameMediaDatabase.addSpriteSeries(weaponMediaInfo.explosionAnim->gafName, weaponMediaInfo.explosionAnim->animName, anim);
                            }
                            if (weaponMediaInfo.waterExplosionAnim)
                            {
                                auto anim = services.textureService->getGafEntry("anims/" + weaponMediaInfo.waterExplosionAnim->gafName + ".gaf", weaponMediaInfo.waterExplosionAnim->animName);
                                dataMaps.gameMediaDatabase.addSpriteSeries(weaponMediaInfo.waterExplosionAnim->gafName, weaponMediaInfo.waterExplosionAnim->animName, anim);
                            }
                        }

                        dataMaps.gameMediaDatabase.addWeapon(toUpper(pair.first), std::move(weaponMediaInfo));

                        dataMaps.weaponDefinitions.insert({toUpper(pair.first), std::move(weaponDefinition)});
                    }
                }
            }

            std::unordered_set<std::string> requiredFeaturesSet;
            for (const auto& f : requiredFeatures)
            {
                requiredFeaturesSet.insert(toUpper(f));
            }

            // read unit FBIs
            {
                auto fbis = services.vfs->getFileNames(services.pathMapping->units, ".fbi");

                // The listing is the data set's load order: TA numbers each
                // unit type by the position its file would be handed in
                // (docs/TOTALA-EXE.md section 109), and the sorted stem order
                // is what that assignment comes out as (tadUnitLoadOrder has
                // the evidence). Captured here because this is the only place
                // the listing exists and the demo recorder needs it later.
                std::vector<std::string> fbiStems;
                fbiStems.reserve(fbis.size());
                for (const auto& fbiName : fbis)
                {
                    fbiStems.push_back(std::filesystem::path(fbiName).stem().string());
                }
                dataMaps.unitLoadOrder = tadUnitLoadOrder(std::move(fbiStems));

                for (const auto& fbiName : fbis)
                {
                    auto bytes = services.vfs->readFile(services.pathMapping->units + "/" + fbiName);
                    if (!bytes)
                    {
                        throw std::runtime_error("File in listing could not be read: " + fbiName);
                    }

                    std::string fbiString(bytes->data(), bytes->size());
                    auto fbi = parseUnitFbi(parseTdfFromString(fbiString));

                    if (auto warning = transportCapacityWarning(fbi))
                    {
                        LOG_WARN << *warning;
                    }
                    if (auto warning = defaultMissionWarning(fbi))
                    {
                        LOG_WARN << *warning;
                    }
                    auto unitDefinition = parseUnitDefinition(fbi, dataMaps.movementClassDatabase);
                    dataMaps.unitDefinitions.insert({toUpper(fbi.unitName), std::move(unitDefinition)});

                    // Read the unit's gui pages if it has any. This used to be
                    // gated on Builder=1, which is wrong: the six launchers that
                    // stockpile a round -- ARMSILO, CORSILO, ARMAMD, CORFMD,
                    // ARMEMP, CORTRON -- all say Builder=0 in their FBI and all
                    // ship a page of their own, whose single live gadget is the
                    // MAKENUKE or MAKEANTI button that orders the round. Gating on
                    // the flag left those pages on disk and the button with no way
                    // to reach it. A page only exists if <unitname><n>.GUI does, so
                    // asking for one costs a failed VFS lookup per unit and nothing
                    // more.
                    auto guiPages = loadBuilderGui(services, fbi.unitName);
                    if (guiPages)
                    {
                        dataMaps.builderGuisDatabase.addBuilderGui(fbi.unitName, std::move(*guiPages));
                    }

                    auto meshInfo = services.meshService->loadUnitMesh(fbi.objectName);
                    dataMaps.modelDefinitions.insert({toUpper(fbi.objectName), std::move(meshInfo.modelDefinition)});
                    for (const auto& m : meshInfo.pieceMeshes)
                    {
                        dataMaps.gameMediaDatabase.addUnitPieceMesh(fbi.objectName, m.first, m.second);
                    }

                    dataMaps.gameMediaDatabase.addSelectionCollisionMesh(fbi.objectName, std::make_shared<CollisionMesh>(std::move(meshInfo.selectionMesh.collisionMesh)));
                    dataMaps.gameMediaDatabase.addSelectionQuad(fbi.objectName, meshInfo.selectionMesh.corners);

                    if (!fbi.corpse.empty())
                    {
                        requiredFeaturesSet.insert(toUpper(fbi.corpse));
                    }
                }
            }

            // The buttons the expansions and patches add to builders that already
            // ship pages, through download/*.tdf rather than new GUI files. Read
            // once every builder's own pages are in, because an entry can land
            // on one of those pages as well as past the end of them. See
            // DownloadMenus.h.
            {
                std::vector<DownloadMenuEntry> downloadEntries;
                for (const auto& name : services.vfs->getFileNames(services.pathMapping->downloads, ".tdf"))
                {
                    auto bytes = services.vfs->readFile(services.pathMapping->downloads + "/" + name);
                    if (!bytes)
                    {
                        continue;
                    }
                    try
                    {
                        auto entries = parseDownloadMenuEntries(parseListTdfFromBytes(*bytes));
                        downloadEntries.insert(downloadEntries.end(), entries.begin(), entries.end());
                    }
                    catch (const std::exception& e)
                    {
                        // Six of these come from third-party downloadable units;
                        // one that will not parse costs its own buttons, not the load.
                        LOG_WARN << "Skipping download menu file " << name << ": " << e.what();
                    }
                }
                auto placed = applyDownloadMenuEntries(dataMaps.builderGuisDatabase, downloadEntries);
                LOG_INFO << "Download menus: " << placed.placed << " buttons placed, " << placed.skipped << " skipped";
            }

            // read feature TDFs
            {
                auto files = services.vfs->getFileNamesRecursive("features", ".tdf");

                std::unordered_map<std::string, FeatureTdf> featureTdfs;

                for (const auto& name : files)
                {
                    auto bytes = services.vfs->readFile("features/" + name);
                    if (!bytes)
                    {
                        throw std::runtime_error("Failed to read feature " + name);
                    }

                    std::string tdfString(bytes->data(), bytes->size());

                    auto tdfRoot = parseTdfFromString(tdfString);
                    for (const auto& e : tdfRoot.blocks)
                    {
                        auto featureTdf = parseFeatureTdf(*e.second);
                        featureTdfs.insert({toUpper(e.first), featureTdf});
                    }
                }

                // actually parse and load assets for features that we require
                for (const auto& featureName : requiredFeaturesSet)
                {
                    loadFeature(services, dataMaps, featureTdfs, featureName);
                }
            }

            // preload smoke
            if (services.textureService != nullptr)
            {
                auto anim = services.textureService->getGafEntry("anims/FX.GAF", "smoke 1");
                dataMaps.gameMediaDatabase.addSpriteSeries("FX", "smoke 1", anim);
                auto anim2 = services.textureService->getGafEntry("anims/FX.GAF", "smoke 2");
                dataMaps.gameMediaDatabase.addSpriteSeries("FX", "smoke 2", anim2);

                // preload weapon sprites
                dataMaps.gameMediaDatabase.addSpriteSeries("FX", "cannonshell", services.textureService->getGafEntry("anims/FX.GAF", "cannonshell"));
                dataMaps.gameMediaDatabase.addSpriteSeries("FX", "plasmasm", services.textureService->getGafEntry("anims/FX.GAF", "plasmasm"));
                dataMaps.gameMediaDatabase.addSpriteSeries("FX", "plasmamd", services.textureService->getGafEntry("anims/FX.GAF", "plasmamd"));
                dataMaps.gameMediaDatabase.addSpriteSeries("FX", "ultrashell", services.textureService->getGafEntry("anims/FX.GAF", "ultrashell"));
                dataMaps.gameMediaDatabase.addSpriteSeries("FX", "flamestream", services.textureService->getGafEntry("anims/FX.GAF", "flamestream"));

                // Explosion and fire sprites used by exploding unit pieces (COB `explode`).
                for (const auto& name : {"Explosion", "Explode2", "Explode3", "Explode4", "Explode5", "Nuke1", "fire1"})
                {
                    if (auto anim = services.textureService->tryGetGafEntry("anims/FX.GAF", name))
                    {
                        dataMaps.gameMediaDatabase.addSpriteSeries("FX", name, *anim);
                    }
                }

                // The strip the Space key slides up from the bottom of the screen
                // (TOTALA-EXE.md S:108): commongui's LIGHTBAR, of which the original
                // draws frame 1, 507 by 32.
                if (auto anim = services.textureService->tryGetGafEntry("anims/commongui.gaf", "LIGHTBAR"))
                {
                    dataMaps.gameMediaDatabase.addSpriteSeries("COMMONGUI", "LIGHTBAR", *anim);
                }

                // In-game titles: TA's own PAUSED / VICTORY / DEFEAT artwork.
                for (const auto& name : {"igpaused", "igvictory", "igdefeat"})
                {
                    auto anim = services.textureService->getGafEntry("anims/IGTITLES.GAF", name);
                    dataMaps.gameMediaDatabase.addSpriteSeries("IGTITLES", name, anim);
                }
            }

            return dataMaps;
        }
    }

    MapData readMapData(TntArchive& tnt, const OtaRecord& ota, unsigned int schemaIndex)
    {
        auto dataGrid = getMapData(tnt);

        Grid<TntTileAttributes> mapAttributes(tnt.getHeader().width, tnt.getHeader().height);
        tnt.readMapAttributes(mapAttributes.getData());

        auto heightGrid = getHeightGrid(mapAttributes);

        MapTerrain terrain(
            std::move(heightGrid),
            SimScalar(tnt.getHeader().seaLevel));

        const auto& schema = ota.schemas.at(schemaIndex);

        auto featureNames = getFeatureNames(tnt);
        std::vector<std::pair<Point, std::string>> features;

        mapAttributes.forEachIndexed([&](auto c, const auto& e) {
            switch (e.feature)
            {
                case TntTileAttributes::FeatureNone:
                case TntTileAttributes::FeatureUnknown:
                case TntTileAttributes::FeatureVoid:
                    break;
                default:
                    features.emplace_back(Point(c.x, c.y), featureNames.at(e.feature));
            }
        });

        // add features from the OTA schema
        for (const auto& f : schema.features)
        {
            features.emplace_back(Point(f.xPos, f.zPos), f.featureName);
        }

        return MapData{
            std::move(terrain),
            static_cast<unsigned char>(schema.surfaceMetal),
            ota.minWindSpeed,
            ota.maxWindSpeed,
            ota.tidalStrength,
            ota.killMul,
            ota.timeMul,
            std::move(features),
            std::move(dataGrid)};
    }

    LoadedGame loadGameSimulation(
        const GameLoadServices& services,
        const GameParameters& gameParameters,
        MapData mapData,
        const OtaRecord& ota)
    {
        std::unordered_set<std::string> requiredFeatureNames;
        for (const auto& f : mapData.features)
        {
            requiredFeatureNames.insert(f.second);
        }

        auto dataMaps = loadGameData(services, requiredFeatureNames);

        auto movementClassCollisionService = createMovementClassCollisionService(mapData.terrain, dataMaps.movementClassDatabase);

        // The wind speed range goes through as the map wrote it. The original
        // caps what a generator can make out of it by clamping the ratio, not
        // the speed, so clamping the speed here as well would have left a map
        // whose minimum is above the cap with an empty range to draw from.
        GameSimulation simulation(std::move(mapData.terrain), mapData.surfaceMetal, std::max(0, mapData.minWindSpeed), std::max(0, mapData.maxWindSpeed));
        simulation.tidalStrength = std::max(0, mapData.tidalStrength);
        simulation.killMul = mapData.killMul;
        simulation.timeMul = mapData.timeMul;
        simulation.noSeaLevelTrigger = ota.noSeaLevelTrigger;

        // The skirmish options the simulation itself has to know about. They
        // go in before any player is added: Mapped hands a player its explored
        // grid at the moment that grid is created. Start Location is not among
        // them because it is spent here, dealing out the map's start
        // positions, and never consulted again.
        simulation.lineOfSightMode = gameParameters.lineOfSight;
        simulation.mappingMode = gameParameters.mapping;
        simulation.commanderDeathMode = gameParameters.commanderDeath;

        simulation.unitDefinitions = std::move(dataMaps.unitDefinitions);
        simulation.weaponDefinitions = std::move(dataMaps.weaponDefinitions);

        // A death weapon that does not exist. The shipped data has one:
        // eleven units -- eight of Core Contingency's, the torpedo seaplanes
        // among them, and three of the v3.1 patch's -- say
        // ExplodeAs=MEDIUM_UNITEX, and no weapon file anywhere defines it
        // (MEDIUM_UNIT is what was meant). The original evidently shrugs,
        // since those units die in it without incident. RWE looked the name
        // up unchecked the moment one died and the game ended with
        // "unordered_map::at" -- found the day the AI first built seaplanes.
        // Cleared here, once, so neither the simulation nor the scene has a
        // name to trip on: the unit dies with no blast, as it must there.
        for (auto& [unitType, definition] : simulation.unitDefinitions)
        {
            for (auto* deathWeapon : {&definition.explodeAs, &definition.selfDestructAs})
            {
                if (!deathWeapon->empty() && simulation.weaponDefinitions.count(*deathWeapon) == 0
                    && simulation.weaponDefinitions.count(toUpper(*deathWeapon)) == 0)
                {
                    LOG_WARN << "Unit " << unitType << " dies as " << *deathWeapon << ", which no weapon file defines; it will die without a blast";
                    deathWeapon->clear();
                }
            }
        }
        simulation.movementClassDatabase = std::move(dataMaps.movementClassDatabase);
        simulation.movementClassCollisionService = std::move(movementClassCollisionService);
        simulation.unitModelDefinitions = dataMaps.modelDefinitions;
        simulation.unitScriptDefinitions = loadCobScripts(*services.vfs);
        simulation.featureDefinitions = std::move(dataMaps.featureDefinitions);
        simulation.featureNameIndex = std::move(dataMaps.featureNameIndex);
        simulation.losTables = std::move(dataMaps.losTables);

        // Two features drawn on one cell: the original keeps the later one
        // unless the earlier is indestructible, and never places anything
        // on the attribute grid's last row or column. Settled here, in the
        // order the map lists them, so addFeature below never has to refuse
        // one -- when it refused, it kept the earlier feature, which on
        // twenty-seven of the official maps is the wrong one.
        const auto& heightmap = simulation.terrain.getHeightMap();
        auto placement = resolveFeatureOverlaps(
            mapData.features,
            heightmap.getWidth() - 1,
            heightmap.getHeight() - 1,
            [&](const std::string& name) {
                const auto& d = simulation.getFeatureDefinition(simulation.tryGetFeatureDefinitionId(name).value());
                return FeaturePlacementInfo{d.footprintX, d.footprintZ, d.indestructible};
            });
        LOG_INFO << "Map features: " << placement.placed.size() << " placed, "
                 << placement.replaced << " replaced by a later feature, "
                 << placement.dropped << " dropped";

        for (const auto& [pos, featureName] : placement.placed)
        {
            auto featureId = simulation.tryGetFeatureDefinitionId(featureName).value();
            if (!simulation.addFeature(featureId, pos.x, pos.y))
            {
                LOG_WARN << "Map feature " << featureName << " at " << pos.x << "," << pos.y << " could not be placed";
            }
        }

        auto seedSeq = seedFromGameParameters(gameParameters);
        simulation.rng.seed(seedSeq);

        std::optional<PlayerId> localPlayerId;

        std::array<std::optional<PlayerId>, 10> gamePlayers;

        for (Index i = 0; i < getSize(gameParameters.players); ++i)
        {
            const auto& params = gameParameters.players[i];
            if (params)
            {
                auto playerType = std::visit(IsComputerVisitor(), params->controller) ? GamePlayerType::Computer : GamePlayerType::Human;
                auto metal = params->metal;
                auto energy = params->energy;
                if (gameParameters.mission)
                {
                    // The mission, not the lobby, says what everyone starts
                    // with: the schema's HumanMetal and HumanEnergy for a
                    // player at the keyboard and ComputerMetal and
                    // ComputerEnergy for the rest.
                    const auto& schema = ota.schemas.at(gameParameters.schemaIndex);
                    auto isComputer = playerType == GamePlayerType::Computer;
                    metal = Metal(static_cast<float>(isComputer ? schema.computerMetal : schema.humanMetal));
                    energy = Energy(static_cast<float>(isComputer ? schema.computerEnergy : schema.humanEnergy));
                }
                GamePlayerInfo gpi{params->name, playerType, params->color, GamePlayerStatus::Alive, params->side, metal, energy, metal, energy, metal, energy, params->teamId};
                gpi.hasBaseStorage = gameParameters.mission;
                auto playerId = simulation.addPlayer(gpi);
                gamePlayers[i] = playerId;

                if (std::visit(IsHumanVisitor(), params->controller))
                {
                    if (localPlayerId)
                    {
                        throw std::runtime_error("Multiple local human players found");
                    }

                    localPlayerId = gamePlayers[i];
                }
            }
        }
        if (!localPlayerId && (gameParameters.aiArenaSeconds || gameParameters.replayFile))
        {
            // Nobody is playing: this is a measurement run, or a recording of
            // a game between computer players being watched back. The scene
            // still needs a point of view, because the camera, the fog it
            // draws and the interface all hang off a local player, so the
            // first slot stands in. It keeps its AI controller -- GameScene
            // knows not to push an empty command buffer on top of the AI's
            // for a local player that is a computer -- so this really is
            // every player being played by the AI.
            for (Index i = 0; i < getSize(gamePlayers); ++i)
            {
                if (gamePlayers[i])
                {
                    localPlayerId = gamePlayers[i];
                    LOG_INFO << "AI arena: no human player, watching from slot " << i;
                    break;
                }
            }
        }
        if (!localPlayerId)
        {
            throw std::runtime_error("No local player!");
        }

        // What the map looks like, read once before anyone moves. Every AI
        // gets the same copy: it describes ground, not a player's situation.
        //
        // The start positions are in here because a human has them too -- the
        // lobby draws them on the map preview. Which one the enemy actually
        // took is not, and cannot be, since the AI is handed the list and not
        // the deal; it has to scout for that like anybody else.
        std::vector<SimVector> declaredStartPositions;
        {
            const auto& startSchema = ota.schemas.at(gameParameters.schemaIndex);
            // StartPos keys are 1-based and a map may leave gaps in them, so
            // scan the whole range the slot table can hold rather than
            // stopping at the first one missing.
            for (int n = 1; n <= 10; ++n)
            {
                auto it = findStartPosition(startSchema, n);
                if (!it)
                {
                    continue;
                }
                auto world = simulation.terrain.topLeftCoordinateToWorld(SimVector(SimScalar(it->xPos), 0_ss, SimScalar(it->zPos)));
                world.y = simulation.terrain.getHeightAt(world.x, world.z);
                declaredStartPositions.push_back(world);
            }
        }
        auto mapIntel = analyseMap(simulation.terrain, std::move(declaredStartPositions));
        LOG_INFO << "Map " << gameParameters.mapName << " reads as " << mapCharacterName(mapIntel.character)
                 << " (" << static_cast<int>(mapIntel.waterFraction * 100.0f) << "% water, "
                 << mapIntel.startPositions.size() << " start positions)";

        // What each builder is allowed to build, read out of the same build
        // menus the human's panels are drawn from. The engine enforces no
        // tech tree of its own -- a BuildOrder naming any type at all becomes
        // a nanoframe -- so without this the AI quietly builds things no
        // player could order from that unit, and cannot know that the
        // advanced constructor is the only way to a fusion plant. See
        // docs/ai-architecture-proposal.md §15.2.
        // A mission offers only the units its `useonlyunits` file lists
        // (0x431740; issue #381). A list that does not open restricts
        // nothing, as in the original: two shipped missions name one that
        // does not exist.
        if (gameParameters.mission && !ota.useOnlyUnits.empty())
        {
            auto listName = ota.useOnlyUnits;
            if (listName.size() < 4 || toUpper(listName.substr(listName.size() - 4)) != ".TDF")
            {
                listName += ".tdf";
            }
            auto bytes = services.vfs->readFile("camps/useonly/" + listName);
            if (!bytes)
            {
                LOG_WARN << "Mission unit list " << listName << " not found; every unit is available";
            }
            else
            {
                try
                {
                    auto allowed = missionUnitListFromTdf(parseTdfFromString(std::string(bytes->begin(), bytes->end())));
                    auto kept = applyMissionUnitList(simulation.unitDefinitions, dataMaps.builderGuisDatabase, allowed);
                    LOG_INFO << "Mission unit list " << listName << ": " << kept << " of " << simulation.unitDefinitions.size() << " units available";
                }
                catch (const std::exception& e)
                {
                    LOG_WARN << "Mission unit list " << listName << " could not be read: " << e.what() << "; every unit is available";
                }
            }
        }

        std::set<std::string> knownUnitTypes;
        for (const auto& [unitType, unitDefinition] : simulation.unitDefinitions)
        {
            if (!unitDefinition.excludedByMission)
            {
                knownUnitTypes.insert(unitType);
            }
        }
        auto buildTree = buildTreeFromBuilderGuis(dataMaps.builderGuisDatabase, knownUnitTypes);
        LOG_INFO << "AI build tree: " << buildTree.buildableBy.size() << " builders with a build menu";

        // Read once, and only if some seat asks for a personality.
        std::optional<std::vector<AiPersonality>> aiPersonalities;

        // Instantiate one AiPlayerController per Computer player.
        // The controller's RNG is sub-seeded from simulation.rng so its
        // sequence is part of the seeded sim and survives replays
        // (docs/ai-architecture-proposal.md §12-Q6).
        for (Index i = 0; i < getSize(simulation.players); ++i)
        {
            const auto& player = simulation.players[i];
            if (player.type != GamePlayerType::Computer)
            {
                continue;
            }
            PlayerId aiPlayerId(i);

            // The battle harness drives every unit itself. An AI here
            // would take its own side over and the fight would stop
            // being the thing under test.
            if (gameParameters.battleTestUnitsPerSide)
            {
                continue;
            }

            // The personality chosen for this seat, if any. The seat is the
            // lobby's slot, which is not the player's index once an empty
            // slot has been skipped.
            std::optional<AiPersonality> personality;
            for (Index slot = 0; slot < getSize(gamePlayers); ++slot)
            {
                if (gamePlayers[slot] != aiPlayerId || !gameParameters.players[slot] || !gameParameters.players[slot]->aiPersonality)
                {
                    continue;
                }
                const auto& name = *gameParameters.players[slot]->aiPersonality;
                if (!aiPersonalities)
                {
                    aiPersonalities = loadAiPersonalities(aiPersonalityDirectory());
                }
                personality = findAiPersonality(*aiPersonalities, name);
                if (!personality)
                {
                    // A saved game can outlive the file it was played with;
                    // the game still loads, as the difficulty's own.
                    LOG_WARN << "Player " << i << ": no AI personality called " << name << ", playing the default";
                }
            }

            // Every computer player in a game shares the difficulty chosen for
            // the game (--ai-difficulty, rwe.cfg or the skirmish menu), unless
            // its personality names one of its own.
            auto profile = makeProfileForDifficulty(personality && personality->difficulty ? *personality->difficulty : gameParameters.aiDifficulty);
            // What this player's faction plays differently, before its
            // personality and any --ai-tune, so that either can set a knob
            // back.
            applyFactionDefaults(profile, player.side);
            if (personality)
            {
                if (auto knob = applyAiPersonality(profile, *personality))
                {
                    LOG_WARN << "Player " << i << ": the " << personality->name << " personality sets " << *knob << ", which is no AI knob";
                }
                LOG_INFO << "Player " << i << " plays the " << personality->name << " personality";
            }
            for (const auto& entry : gameParameters.aiTuning)
            {
                auto colon = entry.find(':');
                auto equals = entry.find('=');
                if (colon == std::string::npos || equals == std::string::npos || equals < colon)
                {
                    throw std::runtime_error("--ai-tune wants <player>:<knob>=<value>, got " + entry);
                }
                if (std::stoul(entry.substr(0, colon)) != static_cast<unsigned long>(i))
                {
                    continue;
                }
                auto knob = entry.substr(colon + 1, equals - colon - 1);
                auto value = entry.substr(equals + 1);
                // A misspelt knob that silently did nothing would make an
                // arena comparison between two identical AIs look like a
                // result, so it is fatal.
                if (!applyAiTuning(profile, knob, value))
                {
                    throw std::runtime_error("--ai-tune: no such AI knob: " + knob);
                }
                LOG_INFO << "Player " << i << " AI knob " << knob << " = " << value;
            }
            if (gameParameters.replayFile && !gameParameters.replayShadowAi)
            {
                // Watching rather than playing: the commands come out of the
                // file, so a thinking AI would only add its own on top.
                //
                // The controller is still CONSTRUCTED, and that is the whole
                // point of idling it here rather than skipping it. Building
                // one draws a value from simulation.rng, and the start
                // positions are dealt from that same stream a few lines
                // further down -- skip the draw and every commander spawns
                // somewhere else, which is a divergence on the very first
                // tick and looks like anything except an off-by-one in the
                // random number generator.
                profile.idle = true;
            }
            LOG_INFO << "Player " << i << " is a computer player at " << aiDifficultyName(profile.difficulty) << " difficulty";

            // Pull a single value from the sim RNG to seed the AI's
            // sub-RNG. This keeps AI choices reproducible across clients
            // that share `simulation.rng`'s seed.
            const std::uint64_t aiSeed = static_cast<std::uint64_t>(simulation.rng());

            simulation.addAiController(
                aiPlayerId,
                std::make_unique<AiPlayerController>(aiPlayerId, std::move(profile), aiSeed, mapIntel, buildTree));
        }

        // Which of the map's start positions each filled slot takes. Fixed
        // leaves slot n on the map's StartPos n; Random permutes that same
        // set, so every position is still used exactly once and none is
        // invented. The deal is drawn from the simulation's RNG, which every
        // peer seeded identically, so everyone lays the players out the same
        // way. It has to happen here, while the simulation is still ours --
        // a few lines further down it is moved into the GameScene.
        std::vector<Index> filledSlots;
        std::vector<int> mapStartPositions;
        for (Index i = 0; i < getSize(gameParameters.players); ++i)
        {
            if (gameParameters.players[i])
            {
                filledSlots.push_back(i);
                mapStartPositions.push_back(static_cast<int>(i) + 1);
            }
        }

        auto dealtStartPositions = dealStartPositions(mapStartPositions, gameParameters.startLocation, simulation.rng);

        std::array<std::optional<int>, 10> startPositionForSlot;
        for (Index k = 0; k < getSize(filledSlots); ++k)
        {
            startPositionForSlot[filledSlots[k]] = dealtStartPositions[k];
        }

        return LoadedGame{
            std::move(simulation),
            gameParameters,
            ota,
            localPlayerId,
            gamePlayers,
            startPositionForSlot,
            std::move(mapIntel),
            std::move(buildTree),
            std::move(dataMaps)};
    }

    namespace
    {
        /**
         * The standing orders an `o` writes: 0, 1, 2 for hold, maneuver,
         * roam and hold, return, at will. The original stores two bits of
         * each; a 3 there is nothing the order buttons know, and is read
         * here as the last of the three.
         */
        UnitMovementOrders missionMoveOrders(float n)
        {
            switch (static_cast<int>(n) & 3)
            {
                case 0:
                    return UnitMovementOrders::HoldPosition;
                case 1:
                    return UnitMovementOrders::Maneuver;
                default:
                    return UnitMovementOrders::Roam;
            }
        }

        UnitFireOrders missionFireOrders(float n)
        {
            switch (static_cast<int>(n) & 3)
            {
                case 0:
                    return UnitFireOrders::HoldFire;
                case 1:
                    return UnitFireOrders::ReturnFire;
                default:
                    return UnitFireOrders::FireAtWill;
            }
        }

        /**
         * A unit's InitialMission, read the way the interpreter 0x487BF0 reads
         * it (TOTALA-EXE-DATA.md §114), into its standing orders, a transport
         * to start aboard, and the list of steps its script will run.
         *
         * The unit is held -- out of the player's hands and the computer's --
         * from the moment the list queues anything (0x487E69), until a
         * MAKESELECTABLE runs: an `s`, or the one appended to a list with no
         * `s`, `p`, point `a` or `d` in it (0x487E76).
         */
        void readInitialMission(
            GameSimulation& simulation,
            UnitId unitId,
            const OtaMissionUnit& record,
            const std::function<std::optional<UnitId>(const std::string&)>& resolveName,
            MissionScript& script)
        {
            using OK = MissionOrder::Kind;
            using SK = MissionStep::Kind;
            auto& unit = simulation.getUnitState(unitId);
            const auto& def = simulation.unitDefinitions.at(unit.unitType);

            // The interpreter's x and z are locals it never clears, so an
            // order missing a coordinate takes the last one given (and, for
            // the first order in the string, whatever was on the stack: 0
            // here).
            float lastX = 0.0f;
            float lastZ = 0.0f;
            auto pointFrom = [&](const std::vector<float>& numbers, std::size_t first) {
                if (numbers.size() > first)
                {
                    lastX = numbers[first];
                }
                if (numbers.size() > first + 1)
                {
                    lastZ = numbers[first + 1];
                }
                auto world = simulation.terrain.topLeftCoordinateToWorld(SimVector(SimScalar(lastX), 0_ss, SimScalar(lastZ)));
                world.y = simulation.terrain.getHeightAt(world.x, world.z);
                return world;
            };
            auto typeKnown = [&](const std::string& name) { return simulation.unitDefinitions.find(toUpper(name)) != simulation.unitDefinitions.end(); };

            bool queued = false;
            bool sticky = false;
            auto push = [&](SK kind) -> MissionStep& {
                MissionStep step;
                step.kind = kind;
                script.steps.push_back(std::move(step));
                return script.steps.back();
            };

            for (const auto& order : record.orders)
            {
                switch (order.kind)
                {
                    case OK::Move:
                        push(SK::Move).position = pointFrom(order.numbers, 0);
                        queued = true;
                        break;
                    case OK::Patrol:
                        push(SK::Patrol).position = pointFrom(order.numbers, 0);
                        queued = true;
                        sticky = true;
                        break;
                    case OK::AttackPoint:
                        push(SK::AttackPoint).position = pointFrom(order.numbers, 0);
                        queued = true;
                        sticky = true;
                        break;
                    case OK::AttackType:
                        if (typeKnown(order.name))
                        {
                            push(SK::AttackType).unitType = toUpper(order.name);
                            queued = true;
                        }
                        break;
                    case OK::Guard:
                        if (auto target = resolveName(order.name))
                        {
                            push(SK::Guard).target = *target;
                            queued = true;
                        }
                        break;
                    case OK::Link:
                        // Aboard that transport from the start, queuing
                        // nothing (0x48AAC0 at once).
                        if (auto carrier = resolveName(order.name))
                        {
                            simulation.loadUnitIntoTransport(*carrier, unitId, std::string());
                        }
                        break;
                    case OK::StandingOrders:
                        if (!order.numbers.empty())
                        {
                            unit.moveOrders = missionMoveOrders(order.numbers[0]);
                        }
                        if (order.numbers.size() > 1)
                        {
                            unit.fireOrders = missionFireOrders(order.numbers[1]);
                        }
                        break;
                    case OK::Wait:
                    {
                        auto& step = push(SK::Wait);
                        auto seconds = order.numbers.empty() ? 0.0f : order.numbers[0];
                        step.ticks = static_cast<int>(seconds * 30.0f);
                        step.radius = order.numbers.size() > 1 ? static_cast<int>(order.numbers[1]) : 0;
                        queued = true;
                        break;
                    }
                    case OK::WaitForAttack:
                        // Watching the unit named, or itself.
                        push(SK::WaitForAttack).target = order.name.empty() ? std::nullopt : resolveName(order.name);
                        queued = true;
                        break;
                    case OK::Unload:
                        push(SK::Unload).position = pointFrom(order.numbers, 0);
                        queued = true;
                        break;
                    case OK::Build:
                        if (typeKnown(order.name))
                        {
                            if (def.isMobile)
                            {
                                auto& step = push(SK::Build);
                                step.unitType = toUpper(order.name);
                                step.position = pointFrom(order.numbers, 1);
                            }
                            else
                            {
                                auto& step = push(SK::FactoryBuild);
                                step.unitType = toUpper(order.name);
                                step.count = order.numbers.empty() ? 1 : static_cast<int>(order.numbers[0]);
                            }
                            queued = true;
                        }
                        break;
                    case OK::BuildWeapon:
                        // Queued, but not an order that holds the unit (0x488115).
                        push(SK::BuildWeapon).count = order.numbers.empty() ? 1 : static_cast<int>(order.numbers[0]);
                        break;
                    case OK::SelfDestruct:
                        push(SK::SelfDestruct);
                        queued = true;
                        sticky = true;
                        break;
                    case OK::MakeSelectable:
                        push(SK::MakeSelectable);
                        queued = true;
                        sticky = true;
                        break;
                    case OK::Skip:
                        break;
                }
            }

            if (queued)
            {
                unit.heldByMission = true;
                if (!sticky)
                {
                    push(SK::MakeSelectable);
                }
            }
            else if (simulation.getPlayer(unit.owner).type == GamePlayerType::Computer)
            {
                // Not held, so the computer's AI has it from the first second
                // and gives it its own standing orders (0x408830), whatever
                // an `o` said: maneuver if it can capture, else roam, and fire
                // at will.
                unit.moveOrders = def.canCapture ? UnitMovementOrders::Maneuver : UnitMovementOrders::Roam;
                unit.fireOrders = UnitFireOrders::FireAtWill;
            }
        }
    }

    namespace
    {
        /**
         * The building's position when its footprint starts at cell (x, y):
         * the middle of the footprint, on the ground.
         */
        SimVector missionBuildingPositionAt(const GameSimulation& simulation, int x, int y, const DiscreteRect& footprint)
        {
            auto topLeft = simulation.terrain.heightmapIndexToWorldCorner(x, y);
            SimVector position(
                topLeft.x + ((SimScalar(footprint.width) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss),
                0_ss,
                topLeft.z + ((SimScalar(footprint.height) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss));
            position.y = simulation.terrain.getHeightAt(position.x, position.z);
            return position;
        }

        /**
         * A mission building whose spot is not free, placed anyway where the
         * original would have put it: its creator never asks what is in the
         * way (0x485F50), so a mission's towers stand among its trees. RWE
         * keeps a building and a feature apart, so what is cleared is the
         * scenery -- every blocking feature under the footprint -- and a
         * footprint hanging off the map's edge comes in just far enough to
         * fit. Anything else in the way, a unit or another building, still
         * keeps it out. Issue #377.
         */
        std::optional<UnitId> placeMissionBuildingAnyway(
            GameSimulation& simulation,
            const std::string& unitType,
            const UnitDefinition& def,
            PlayerId owner,
            SimVector position,
            SimAngle heading,
            std::vector<std::string>& notes,
            const std::string& label)
        {
            auto footprint = simulation.computeFootprintRegion(position, def.movementCollisionInfo);
            auto gridWidth = simulation.occupiedGrid.getWidth();
            auto gridHeight = simulation.occupiedGrid.getHeight();
            auto width = static_cast<int>(footprint.width);
            auto height = static_cast<int>(footprint.height);
            if (width > gridWidth || height > gridHeight)
            {
                return std::nullopt;
            }

            // What was done to place it, reported only once it stands: one
            // note, the move and the clearing together when both were needed.
            std::string moved;
            auto x = std::clamp(footprint.x, 0, gridWidth - width);
            auto y = std::clamp(footprint.y, 0, gridHeight - height);
            if (x != footprint.x || y != footprint.y)
            {
                moved = "moved onto the map by " + std::to_string(x - footprint.x) + "," + std::to_string(y - footprint.y) + " cells";
                position = missionBuildingPositionAt(simulation, x, y, footprint);
                footprint = simulation.computeFootprintRegion(position, def.movementCollisionInfo);
                if (auto unitId = simulation.trySpawnUnit(unitType, owner, position, heading))
                {
                    notes.push_back(label + ": " + moved);
                    return unitId;
                }
            }

            auto region = simulation.occupiedGrid.tryToRegion(footprint);
            if (!region)
            {
                return std::nullopt;
            }

            std::vector<FeatureId> scenery;
            auto somethingElse = false;
            region->forEach([&](const auto& coordinates) {
                const auto& cell = simulation.occupiedGrid.get(coordinates);
                if (cell.mobileUnitId || (cell.buildingInfo && !cell.buildingInfo->passable))
                {
                    somethingElse = true;
                }
                if (cell.featureId
                    && simulation.getFeatureDefinition(simulation.getFeature(*cell.featureId).featureName).blocking
                    && std::find(scenery.begin(), scenery.end(), *cell.featureId) == scenery.end())
                {
                    scenery.push_back(*cell.featureId);
                }
            });
            if (somethingElse || scenery.empty())
            {
                return std::nullopt;
            }

            std::string names;
            for (auto featureId : scenery)
            {
                names += (names.empty() ? "" : ", ") + simulation.getFeatureDefinition(simulation.getFeature(featureId).featureName).name;
                simulation.deleteFeature(featureId);
            }
            auto unitId = simulation.trySpawnUnit(unitType, owner, position, heading);
            if (unitId)
            {
                notes.push_back(label + ": " + (moved.empty() ? "" : moved + " and ") + "cleared " + names + " from under it");
            }
            return unitId;
        }
    }

    std::set<std::string> missionUnitListFromTdf(const TdfBlock& tdf)
    {
        std::set<std::string> names;
        for (const auto& [name, block] : tdf.blocks)
        {
            names.insert(toUpper(name));
        }
        return names;
    }

    std::size_t applyMissionUnitList(std::unordered_map<std::string, UnitDefinition>& unitDefinitions, BuilderGuisDatabase& builderGuis, const std::set<std::string>& allowed)
    {
        std::size_t kept = 0;
        for (auto& [unitType, definition] : unitDefinitions)
        {
            definition.excludedByMission = allowed.count(toUpper(unitType)) == 0;
            kept += definition.excludedByMission ? 0 : 1;
        }

        // A build menu's button is named after the unit it builds; the other
        // gadgets on a page -- the page flips, the order buttons -- name no
        // unit and stay.
        for (auto& [builderName, pages] : builderGuis.builderGuisMap)
        {
            for (auto& page : pages)
            {
                page.erase(
                    std::remove_if(page.begin(), page.end(), [&](const GuiEntry& entry) {
                        auto it = unitDefinitions.find(toUpper(entry.common.name));
                        return it != unitDefinitions.end() && it->second.excludedByMission;
                    }),
                    page.end());
            }
        }
        return kept;
    }

    MissionSpawnResult spawnMissionUnits(GameSimulation& simulation, const OtaSchema& schema, const std::array<std::optional<PlayerId>, 10>& slotPlayers)
    {
        MissionSpawnResult result;
        std::vector<std::size_t> spawnedRecords;
        for (std::size_t i = 0; i < schema.units.size(); ++i)
        {
            const auto& record = schema.units[i];
            auto describe = [&](const std::string& why) {
                return "[unit" + std::to_string(i) + "] " + record.unitName + " for player " + std::to_string(record.player) + ": " + why;
            };

            // 0x488A50: the definition by name. An unknown one makes nothing.
            auto unitType = toUpper(record.unitName);
            auto defIt = simulation.unitDefinitions.find(unitType);
            if (defIt == simulation.unitDefinitions.end())
            {
                result.skipped.push_back(describe("no such unit"));
                continue;
            }
            const auto& def = defIt->second;

            // Off the mission's own unit list the original has no definition
            // for it at all, so it makes nothing, as for an unknown name.
            if (def.excludedByMission)
            {
                result.skipped.push_back(describe("not on the mission's unit list"));
                continue;
            }

            // Player N is slot N-1 (0x4883B2). The original logs an unseated
            // one and spawns it regardless; RWE has no player to give it to.
            auto slot = record.player - 1;
            if (slot < 0 || slot >= static_cast<int>(slotPlayers.size()) || !slotPlayers[slot])
            {
                result.skipped.push_back(describe("no player in that slot"));
                continue;
            }

            // Positions are world units from the map's top-left corner, as a
            // start position is.
            auto position = simulation.terrain.topLeftCoordinateToWorld(SimVector(SimScalar(static_cast<float>(record.xPos)), 0_ss, SimScalar(static_cast<float>(record.zPos))));
            if (!def.isMobile)
            {
                // 0x47DDC0: a building goes on the build grid, its footprint
                // on the nearest whole cells, as a building placed by hand
                // does.
                auto rect = simulation.computeFootprintRegion(position, def.movementCollisionInfo);
                auto topLeft = simulation.terrain.heightmapIndexToWorldCorner(rect.x, rect.y);
                position.x = topLeft.x + ((SimScalar(rect.width) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss);
                position.z = topLeft.z + ((SimScalar(rect.height) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss);
            }
            position.y = simulation.terrain.getHeightAt(position.x, position.z);

            // 0x436EF9: degrees to the sixteen-bit angle, truncated.
            auto heading = SimAngle(static_cast<uint16_t>(static_cast<int32_t>((static_cast<int64_t>(record.angle) * 65536) / 360)));

            auto unitId = simulation.trySpawnUnit(unitType, *slotPlayers[slot], position, heading);
            // The original's creator 0x485F50 fails only for want of a unit
            // slot or on the per-type limit, never on where the unit goes,
            // so mission units that overlap -- a truck parked under an
            // aircraft, a unit on a tree -- simply share the ground. RWE's
            // occupancy holds one unit to a cell, so a mobile unit that
            // cannot have its own spot takes the nearest one it can, ring by
            // ring out to eight cells, in a fixed order. A building keeps its
            // place or is not made: moving one would redraw the mission.
            for (int ring = 1; !unitId && def.isMobile && ring <= 8; ++ring)
            {
                for (int dz = -ring; !unitId && dz <= ring; ++dz)
                {
                    for (int dx = -ring; !unitId && dx <= ring; ++dx)
                    {
                        if (std::max(std::abs(dx), std::abs(dz)) != ring)
                        {
                            continue;
                        }
                        auto nearby = position;
                        nearby.x += SimScalar(static_cast<float>(dx)) * MapTerrain::HeightTileWidthInWorldUnits;
                        nearby.z += SimScalar(static_cast<float>(dz)) * MapTerrain::HeightTileHeightInWorldUnits;
                        nearby.y = simulation.terrain.getHeightAt(nearby.x, nearby.z);
                        unitId = simulation.trySpawnUnit(unitType, *slotPlayers[slot], nearby, heading);
                    }
                }
            }
            if (!unitId && !def.isMobile)
            {
                unitId = placeMissionBuildingAnyway(simulation, unitType, def, *slotPlayers[slot], position, heading, result.adjusted, describe("placed anyway"));
            }
            if (!unitId)
            {
                result.skipped.push_back(describe("could not be placed"));
                continue;
            }
            auto& unit = simulation.getUnitState(*unitId);
            unit.finishBuilding(def);
            // 0x48848E: the maximum times the percentage over a hundred,
            // truncated.
            unit.hitPoints = static_cast<unsigned int>((static_cast<uint64_t>(def.maxHitPoints) * static_cast<uint64_t>(std::max(0, record.healthPercentage))) / 100u);
            result.spawned.push_back(*unitId);
            spawnedRecords.push_back(i);
        }

        // The orders, in a second pass once every unit is made, as the
        // original reads them (0x4884F1). A name is the Ident or the unit
        // type of the first record, in file order, that produced a unit
        // (0x487AF0).
        auto resolveName = [&](const std::string& name) -> std::optional<UnitId> {
            // A name that did not scan resolves to nothing, not to the first
            // record that has no Ident.
            if (name.empty())
            {
                return std::nullopt;
            }
            auto wanted = toUpper(name);
            for (std::size_t n = 0; n < spawnedRecords.size(); ++n)
            {
                const auto& record = schema.units[spawnedRecords[n]];
                if (toUpper(record.ident) == wanted || toUpper(record.unitName) == wanted)
                {
                    return result.spawned[n];
                }
            }
            return std::nullopt;
        };
        for (std::size_t n = 0; n < spawnedRecords.size(); ++n)
        {
            const auto& record = schema.units[spawnedRecords[n]];
            // Immunity is the one record flag the spawner copies (0x488475).
            simulation.getUnitState(result.spawned[n]).immune = record.immunity;
            MissionScript script;
            readInitialMission(simulation, result.spawned[n], record, resolveName, script);
            if (!script.steps.empty())
            {
                if (!simulation.missionScripts)
                {
                    simulation.missionScripts = std::make_unique<MissionScripts>();
                }
                simulation.missionScripts->scripts[result.spawned[n].value] = std::move(script);
            }
        }
        return result;
    }

    MissionRules buildMissionRules(
        const OtaMissionRules& rules,
        const MapTerrain& terrain,
        bool hasUnits,
        const std::optional<PlayerId>& human,
        const std::optional<PlayerId>& computer,
        const std::string& humanCommander,
        const std::string& computerCommander)
    {
        using K = MissionRule::Kind;

        MissionRules m;
        m.enabled = hasUnits;
        m.human = human;
        m.computer = computer;
        m.humanCommander = toUpper(humanCommander);
        m.computerCommander = toUpper(computerCommander);

        // ANYTYPE is any type only where the rule's own test says so:
        // MoveUnitToRadius and UnitTypePassesX/Z compare an empty name as a
        // match. Everywhere else the name is looked up or compared as it
        // stands, and a unit called ANYTYPE is what it would take to meet it.
        auto rule = [](K kind, const std::string& unitType = std::string(), int number = 0) {
            MissionRule r;
            r.kind = kind;
            auto upper = toUpper(unitType);
            auto anyTypeMeansAny = kind == K::MoveUnitToRadius || kind == K::UnitTypePassesX || kind == K::UnitTypePassesZ;
            r.unitType = (anyTypeMeansAny && upper == "ANYTYPE") ? std::string() : upper;
            r.number = number;
            return r;
        };
        // Seconds to ticks, without overflowing on a silly value.
        auto ticks = [](int seconds) { return static_cast<int>(std::min<int64_t>(static_cast<int64_t>(seconds) * SimTicksPerSecond, std::numeric_limits<int>::max())); };

        if (rules.killEnemyCommander != 0)
        {
            m.victory.push_back(rule(K::KillEnemyCommander));
        }
        if (rules.destroyAllUnits != 0)
        {
            m.victory.push_back(rule(K::DestroyAllUnits));
        }
        if (rules.killAllMobileUnits != 0)
        {
            m.victory.push_back(rule(K::KillAllMobileUnits));
        }
        if (rules.buildUnitType)
        {
            m.victory.push_back(rule(K::BuildUnitType, *rules.buildUnitType));
        }
        if (rules.captureUnitType)
        {
            m.victory.push_back(rule(K::CaptureUnitType, *rules.captureUnitType));
        }
        if (rules.killAllOfType)
        {
            m.victory.push_back(rule(K::KillAllOfType, *rules.killAllOfType));
        }
        if (rules.killUnitType)
        {
            m.victory.push_back(rule(K::KillUnitType, rules.killUnitType->unitType, rules.killUnitType->number));
        }
        if (rules.moveUnitToRadius)
        {
            auto r = rule(K::MoveUnitToRadius, rules.moveUnitToRadius->unitType);
            auto centre = terrain.topLeftCoordinateToWorld(missionPointOnGround(terrain, rules.moveUnitToRadius->x, rules.moveUnitToRadius->z));
            r.x = centre.x;
            r.z = centre.z;
            r.radius = intToSimScalar(rules.moveUnitToRadius->radius);
            m.victory.push_back(r);
        }
        if (rules.unitTypePassesX)
        {
            m.victory.push_back(rule(K::UnitTypePassesX, rules.unitTypePassesX->unitType, rules.unitTypePassesX->number >> 4));
        }
        if (rules.unitTypePassesZ)
        {
            m.victory.push_back(rule(K::UnitTypePassesZ, rules.unitTypePassesZ->unitType, rules.unitTypePassesZ->number >> 4));
        }
        if (rules.victoryTimerRunsOut > 0)
        {
            m.victory.push_back(rule(K::VictoryTimerRunsOut, std::string(), ticks(rules.victoryTimerRunsOut)));
        }

        if (rules.commanderKilled != 0)
        {
            m.defeat.push_back(rule(K::CommanderKilled));
        }
        if (rules.allUnitsKilled != 0)
        {
            m.defeat.push_back(rule(K::AllUnitsKilled));
        }
        if (rules.allUnitsKilledOfType)
        {
            m.defeat.push_back(rule(K::AllUnitsKilledOfType, *rules.allUnitsKilledOfType));
        }
        if (rules.unitTypeKilled)
        {
            m.defeat.push_back(rule(K::UnitTypeKilled, rules.unitTypeKilled->unitType, rules.unitTypeKilled->number));
        }
        if (rules.deathTimerRunsOut > 0)
        {
            m.defeat.push_back(rule(K::DeathTimerRunsOut, std::string(), ticks(rules.deathTimerRunsOut)));
        }
        if (rules.anyUnitPassesX >= 0)
        {
            m.defeat.push_back(rule(K::AnyUnitPassesX, std::string(), rules.anyUnitPassesX >> 4));
        }
        if (rules.anyUnitPassesZ >= 0)
        {
            m.defeat.push_back(rule(K::AnyUnitPassesZ, std::string(), rules.anyUnitPassesZ >> 4));
        }

        // 0x4902F3 and 0x49041D add these on the first poll; the rules are
        // the same either way.
        if (m.victory.empty())
        {
            m.victory.push_back(rule(K::DestroyAllUnits));
        }
        if (m.defeat.empty())
        {
            m.defeat.push_back(rule(K::AllUnitsKilled));
        }

        return m;
    }

    void installMissionRules(
        GameSimulation& simulation,
        const OtaRecord& ota,
        const OtaSchema& schema,
        const std::array<std::optional<PlayerId>, 10>& slotPlayers,
        const std::unordered_map<std::string, SideData>& sideData)
    {
        auto commanderOf = [&](const std::optional<PlayerId>& player) {
            if (!player)
            {
                return std::string();
            }
            auto it = sideData.find(simulation.getPlayer(*player).side);
            return it == sideData.end() ? std::string() : it->second.commander;
        };
        simulation.missionRules = std::make_unique<MissionRules>(buildMissionRules(
            ota.rules,
            simulation.terrain,
            !schema.units.empty(),
            slotPlayers[0],
            slotPlayers[1],
            commanderOf(slotPlayers[0]),
            commanderOf(slotPlayers[1])));
        const auto& rules = *simulation.missionRules;
        LOG_INFO << "Mission rules: " << rules.victory.size() << " to win, " << rules.defeat.size() << " to lose" << (rules.enabled ? "" : ", switched off: the mission has no [units]");
    }
}
