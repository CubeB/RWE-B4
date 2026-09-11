#include "GameScene_util.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <rwe/render/WireframeScan.h>
#include <rwe/util/Index.h>
#include <rwe/util/match.h>

namespace rwe
{
    UnitDrawStyle computeUnitDrawStyle(bool ownedByViewer, bool cloaked, bool positionVisible)
    {
        if (ownedByViewer)
        {
            return cloaked ? UnitDrawStyle::Cloaked : UnitDrawStyle::Solid;
        }

        if (cloaked)
        {
            return UnitDrawStyle::Hidden;
        }

        return positionVisible ? UnitDrawStyle::Solid : UnitDrawStyle::Hidden;
    }

    Vector3f blendCloakedColor(const Vector3f& unitColor, const Vector3f& backgroundColor)
    {
        return (unitColor * CloakBlendFactor) + (backgroundColor * (1.0f - CloakBlendFactor));
    }

    bool effectIsVisibleToPlayer(const GameSimulation& sim, const PlayerVisibility& visibility, const SimVector& position)
    {
        return visibility.isVisible(sim.visionCellAt(position));
    }

    WeaponImpactEffects computeWeaponImpactEffects(const WeaponMediaInfo& weapon, ImpactType impactType, bool positionVisible)
    {
        if (!positionVisible)
        {
            return WeaponImpactEffects{};
        }

        WeaponImpactEffects effects;
        effects.flash = true;

        switch (impactType)
        {
            case ImpactType::Normal:
                effects.explosion = weapon.explosionAnim;
                effects.smoke = weapon.endSmoke;
                break;
            case ImpactType::Water:
                // No smoke off a splash: the original's smoke emitters check
                // the point against the sea level byte first and emit nothing
                // underwater (S:4).
                effects.explosion = weapon.waterExplosionAnim;
                break;
        }

        return effects;
    }

    Vector3f colorToVector3f(const Color& color)
    {
        return Vector3f(static_cast<float>(color.r) / 255.0f, static_cast<float>(color.g) / 255.0f, static_cast<float>(color.b) / 255.0f);
    }

    void pushTriangle(std::vector<GlColoredVertex>& vs, const Triangle3f& tri, const Vector3f& color)
    {
        vs.emplace_back(tri.a, color);
        vs.emplace_back(tri.b, color);
        vs.emplace_back(tri.c, color);
    }

    void pushTriangle(std::vector<GlColoredVertex>& vs, const Triangle3f& tri)
    {
        pushTriangle(vs, tri, Vector3f(1.0f, 1.0f, 1.0f));
    }

    void pushTriangle(std::vector<GlColoredVertex>& vs, const Vector3f& a, const Vector3f& b, const Vector3f& c)
    {
        pushTriangle(vs, Triangle3f(a, b, c));
    }

    void pushTriangle(std::vector<GlColoredVertex>& vs, const Vector3f& a, const Vector3f& b, const Vector3f& c, const Vector3f& color)
    {
        pushTriangle(vs, Triangle3f(a, b, c), color);
    }

    void pushLine(std::vector<GlColoredVertex>& vs, const Line3f& line, const Vector3f& color)
    {
        vs.emplace_back(line.start, color);
        vs.emplace_back(line.end, color);
    }

    void pushLine(std::vector<GlColoredVertex>& vs, const Line3f& line)
    {
        vs.emplace_back(line.start, Vector3f(1.0f, 1.0f, 1.0f));
        vs.emplace_back(line.end, Vector3f(1.0f, 1.0f, 1.0f));
    }

    void pushLine(std::vector<GlColoredVertex>& vs, const Vector3f& start, const Vector3f& end)
    {
        pushLine(vs, Line3f(start, end));
    }

    void pushLine(std::vector<GlColoredVertex>& vs, const Vector3f& start, const Vector3f& end, const Vector3f& color)
    {
        pushLine(vs, Line3f(start, end), color);
    }

    void
    drawPathfindingVisualisation(const MapTerrain& terrain, const AStarPathInfo<Point, PathCost>& pathInfo, ColoredMeshBatch& batch)
    {
        for (const auto& item : pathInfo.closedVertices)
        {
            if (item.second.predecessor < 0)
            {
                continue;
            }

            auto start = pathInfo.closedVertices[item.second.predecessor].second.vertex;
            auto end = item.second.vertex;
            drawTerrainArrow(terrain, start, end, Color(255, 0, 0), batch);
        }

        if (pathInfo.path.size() > 1)
        {
            for (auto it = pathInfo.path.begin() + 1; it != pathInfo.path.end(); ++it)
            {
                auto start = *(it - 1);
                auto end = *it;
                drawTerrainArrow(terrain, start, end, Color(0, 0, 255), batch);
            }
        }
    }

    void
    drawTerrainArrow(const MapTerrain& terrain, const Point& start, const Point& end, const Color& color, ColoredMeshBatch& batch)
    {
        auto worldStart = terrain.heightmapIndexToWorldCenter(start);
        worldStart.y = terrain.getHeightAt(worldStart.x, worldStart.z);
        auto worldStartF = simVectorToFloat(worldStart);

        auto worldEnd = terrain.heightmapIndexToWorldCenter(end);
        worldEnd.y = terrain.getHeightAt(worldEnd.x, worldEnd.z);
        auto worldEndF = simVectorToFloat(worldEnd);

        auto armTemplate = (worldStartF - worldEndF).normalized() * 6.0f;
        auto arm1 = Matrix4f::translation(worldEndF) * Matrix4f::rotationY(Pif / 6.0f) * armTemplate;
        auto arm2 = Matrix4f::translation(worldEndF) * Matrix4f::rotationY(-Pif / 6.0f) * armTemplate;

        auto floatColor = colorToVector3f(color);

        pushLine(batch.lines, worldStartF, worldEndF, floatColor);
        pushLine(batch.lines, worldEndF, arm1, floatColor);
        pushLine(batch.lines, worldEndF, arm2, floatColor);
    }

    std::optional<Vector3f> getOccupiedColor(const OccupiedCell& cell)
    {
        auto r = cell.mobileUnitId ? 1.0f : 0.0f;
        auto g = cell.buildingInfo ? 1.0f : 0.0f;
        auto b = cell.featureId ? 1.0f : 0.0f;
        if (r == 0.0f && g == 0.0f && b == 0.0f)
        {
            return std::nullopt;
        }
        return Vector3f(r, g, b);
    }

    void drawOccupiedGrid(const Vector3f& cameraPosition, float viewportWidth, float viewportHeight, const MapTerrain& terrain, const OccupiedGrid& occupiedGrid, ColoredMeshBatch& batch)
    {
        auto halfWidth = viewportWidth / 2.0f;
        auto halfHeight = viewportHeight / 2.0f;
        auto left = cameraPosition.x - halfWidth;
        auto top = cameraPosition.z - halfHeight;
        auto right = cameraPosition.x + halfWidth;
        auto bottom = cameraPosition.z + halfHeight;

        assert(left < right);
        assert(top < bottom);

        assert(terrain.getHeightMap().getWidth() >= 2);
        assert(terrain.getHeightMap().getHeight() >= 2);

        auto topLeftCell = terrain.worldToHeightmapCoordinate(floatToSimVector(Vector3f(left, 0.0f, top)));
        topLeftCell.x = std::clamp(topLeftCell.x, 0, static_cast<int>(occupiedGrid.getWidth() - 1));
        topLeftCell.y = std::clamp(topLeftCell.y, 0, static_cast<int>(occupiedGrid.getHeight() - 1));

        auto bottomRightCell = terrain.worldToHeightmapCoordinate(floatToSimVector(Vector3f(right, 0.0f, bottom)));
        bottomRightCell.y += 7; // compensate for height
        bottomRightCell.x = std::clamp(bottomRightCell.x, 0, static_cast<int>(occupiedGrid.getWidth() - 1));
        bottomRightCell.y = std::clamp(bottomRightCell.y, 0, static_cast<int>(occupiedGrid.getHeight() - 1));

        assert(topLeftCell.x <= bottomRightCell.x);
        assert(topLeftCell.y <= bottomRightCell.y);

        for (int y = topLeftCell.y; y <= bottomRightCell.y; ++y)
        {
            for (int x = topLeftCell.x; x <= bottomRightCell.x; ++x)
            {
                auto pos = simVectorToFloat(terrain.heightmapIndexToWorldCorner(x, y));
                pos.y = terrain.getHeightMap().get(x, y);

                auto rightPos = simVectorToFloat(terrain.heightmapIndexToWorldCorner(x + 1, y));
                rightPos.y = terrain.getHeightMap().get(x + 1, y);

                auto downPos = simVectorToFloat(terrain.heightmapIndexToWorldCorner(x, y + 1));
                downPos.y = terrain.getHeightMap().get(x, y + 1);

                pushLine(batch.lines, pos, rightPos);
                pushLine(batch.lines, pos, downPos);

                const auto& cell = occupiedGrid.get(x, y);
                if (auto c = getOccupiedColor(cell); c)
                {
                    auto downRightPos = simVectorToFloat(terrain.heightmapIndexToWorldCorner(x + 1, y + 1));
                    downRightPos.y = terrain.getHeightMap().get(x + 1, y + 1);

                    pushTriangle(batch.triangles, pos, downPos, downRightPos, *c);
                    pushTriangle(batch.triangles, pos, downRightPos, rightPos, *c);
                }

                const auto insetAmount = 4.0f;

                if (cell.buildingInfo && !cell.buildingInfo->passable)
                {
                    auto topLeftPos = pos + Vector3f(insetAmount, 0.0f, insetAmount);
                    auto topRightPos = rightPos + Vector3f(-insetAmount, 0.0f, insetAmount);
                    auto bottomLeftPos = downPos + Vector3f(insetAmount, 0.0f, -insetAmount);
                    auto downRightPos = simVectorToFloat(terrain.heightmapIndexToWorldCorner(x + 1, y + 1));
                    downRightPos.y = terrain.getHeightMap().get(x + 1, y + 1);
                    downRightPos += Vector3f(-insetAmount, 0.0f, -insetAmount);

                    pushTriangle(batch.triangles, topLeftPos, bottomLeftPos, downRightPos, Vector3f(1.0f, 0.0f, 0.0f));
                    pushTriangle(batch.triangles, topLeftPos, downRightPos, topRightPos, Vector3f(1.0f, 0.0f, 0.0f));
                }

                if (cell.buildingInfo && cell.buildingInfo->passable)
                {
                    auto topLeftPos = pos + Vector3f(insetAmount, 0.0f, insetAmount);
                    auto topRightPos = rightPos + Vector3f(-insetAmount, 0.0f, insetAmount);
                    auto bottomLeftPos = downPos + Vector3f(insetAmount, 0.0f, -insetAmount);
                    auto downRightPos = simVectorToFloat(terrain.heightmapIndexToWorldCorner(x + 1, y + 1));
                    downRightPos.y = terrain.getHeightMap().get(x + 1, y + 1);
                    downRightPos += Vector3f(-insetAmount, 0.0f, -insetAmount);

                    pushTriangle(batch.triangles, topLeftPos, bottomLeftPos, downRightPos, Vector3f(1.0f, 1.0f, 1.0f));
                    pushTriangle(batch.triangles, topLeftPos, downRightPos, topRightPos, Vector3f(1.0f, 1.0f, 1.0f));
                }
            }
        }
    }

    void drawMovementClassCollisionGrid(
        const MapTerrain& terrain,
        const Grid<char>& movementClassGrid,
        const Vector3f& cameraPosition,
        float viewportWidth,
        float viewportHeight,
        ColoredMeshBatch& batch)
    {
        auto halfWidth = viewportWidth / 2.0f;
        auto halfHeight = viewportHeight / 2.0f;
        auto left = cameraPosition.x - halfWidth;
        auto top = cameraPosition.z - halfHeight;
        auto right = cameraPosition.x + halfWidth;
        auto bottom = cameraPosition.z + halfHeight;

        assert(left < right);
        assert(top < bottom);

        assert(terrain.getHeightMap().getWidth() >= 2);
        assert(terrain.getHeightMap().getHeight() >= 2);
        assert(movementClassGrid.getWidth() <= terrain.getHeightMap().getWidth());
        assert(movementClassGrid.getHeight() <= terrain.getHeightMap().getHeight());

        auto topLeftCell = terrain.worldToHeightmapCoordinate(floatToSimVector(Vector3f(left, 0.0f, top)));
        topLeftCell.x = std::clamp(topLeftCell.x, 0, static_cast<int>(movementClassGrid.getWidth() - 1));
        topLeftCell.y = std::clamp(topLeftCell.y, 0, static_cast<int>(movementClassGrid.getHeight() - 1));

        auto bottomRightCell = terrain.worldToHeightmapCoordinate(floatToSimVector(Vector3f(right, 0.0f, bottom)));
        bottomRightCell.y += 7; // compensate for height
        bottomRightCell.x = std::clamp(bottomRightCell.x, 0, static_cast<int>(movementClassGrid.getWidth() - 1));
        bottomRightCell.y = std::clamp(bottomRightCell.y, 0, static_cast<int>(movementClassGrid.getHeight() - 1));

        assert(topLeftCell.x <= bottomRightCell.x);
        assert(topLeftCell.y <= bottomRightCell.y);

        for (int y = topLeftCell.y; y <= bottomRightCell.y; ++y)
        {
            for (int x = topLeftCell.x; x <= bottomRightCell.x; ++x)
            {
                auto pos = simVectorToFloat(terrain.heightmapIndexToWorldCorner(x, y));
                pos.y = terrain.getHeightMap().get(x, y);

                auto rightPos = simVectorToFloat(terrain.heightmapIndexToWorldCorner(x + 1, y));
                rightPos.y = terrain.getHeightMap().get(x + 1, y);

                auto downPos = simVectorToFloat(terrain.heightmapIndexToWorldCorner(x, y + 1));
                downPos.y = terrain.getHeightMap().get(x, y + 1);

                pushLine(batch.lines, pos, rightPos);
                pushLine(batch.lines, pos, downPos);

                if (!movementClassGrid.get(x, y))
                {
                    auto downRightPos = simVectorToFloat(terrain.heightmapIndexToWorldCorner(x + 1, y + 1));
                    downRightPos.y = terrain.getHeightMap().get(x + 1, y + 1);

                    pushTriangle(batch.triangles, pos, downPos, downRightPos);
                    pushTriangle(batch.triangles, pos, downRightPos, rightPos);
                }
            }
        }
    }

    Matrix4f unitRenderTransform(const UnitState& unit, const UnitDefinition& unitDefinition, const Vector3f& position, float rotation, float frac)
    {
        auto transform = Matrix4f::translation(position) * Matrix4f::rotationY(rotation);

        // Aircraft fly level except where a task asks for a bank — the
        // construction aircraft heeling over between the stations of its
        // work pattern.
        if (unitDefinition.canFly)
        {
            if (auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unit.physics))
            {
                auto roll = airPhysics->previousRoll.value + ((airPhysics->roll.value - airPhysics->previousRoll.value) * frac);
                if (roll != 0.0f)
                {
                    transform = transform * Matrix4f::rotationZ(roll);
                }
            }
        }

        return transform;
    }

    ViewCullTest makeViewCullTest(const Matrix4f& viewProjectionMatrix)
    {
        // How far one world unit moves a point in clip space, taken along
        // each axis in turn and added up rather than combined properly: the
        // sum can only overstate the reach of a radius, and overstating it
        // draws a unit that need not have been drawn, where understating it
        // would drop one that should have been.
        auto origin = viewProjectionMatrix * Vector3f(0.0f, 0.0f, 0.0f);
        auto perWorldUnit = 0.0f;
        for (const auto& axis : {Vector3f(1.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f)})
        {
            auto step = (viewProjectionMatrix * axis) - origin;
            perWorldUnit += std::max(std::abs(step.x), std::abs(step.y));
        }

        return ViewCullTest{viewProjectionMatrix, perWorldUnit};
    }

    namespace
    {
        /**
         * Where the piece transforms of the model being drawn are put. The
         * renderer runs on one thread and every loop below fills this, reads
         * it and is finished with it before the next model, so one buffer
         * serves them all and no frame allocates for it after the first
         * unit of the largest model in the game.
         */
        thread_local std::vector<Matrix4f> pieceTransformScratch;
    }

    const std::vector<Matrix4f>& computePieceTransformsForRender(
        const UnitModelDefinition& modelDefinition,
        const UnitModelRenderInfo& renderInfo,
        const std::vector<UnitMesh>& pieces,
        float frac)
    {
        assert(modelDefinition.pieces.size() == pieces.size());

        // One pass down the hierarchy rather than a walk back up to the root
        // for every piece: a parent's transform is already worked out by the
        // time its children are reached, so each piece costs one matrix
        // multiply instead of one per link above it, and none of it touches a
        // string.
        pieceTransformScratch.resize(pieces.size());
        for (auto i : renderInfo.evaluationOrder)
        {
            const auto& pieceDef = modelDefinition.pieces[i];
            const auto& pieceState = pieces[i];

            auto position = lerp(simVectorToFloat(pieceDef.origin + pieceState.previousOffset), simVectorToFloat(pieceDef.origin + pieceState.offset), frac);
            auto rotationX = angleLerp(toRadians(pieceState.previousRotationX).value, toRadians(pieceState.rotationX).value, frac);
            auto rotationY = angleLerp(toRadians(pieceState.previousRotationY).value, toRadians(pieceState.rotationY).value, frac);
            auto rotationZ = angleLerp(toRadians(pieceState.previousRotationZ).value, toRadians(pieceState.rotationZ).value, frac);

            auto local = Matrix4f::translation(position) * Matrix4f::rotationZXY(Vector3f(rotationX, rotationY, rotationZ));
            auto parent = renderInfo.parentIndices[i];
            pieceTransformScratch[i] = parent < 0 ? local : pieceTransformScratch[parent] * local;
        }

        return pieceTransformScratch;
    }

    void drawShaderMesh(
        const Matrix4f& viewProjectionMatrix,
        const ShaderMesh& mesh,
        const Matrix4f& matrix,
        float shadeStrength,
        PlayerColorIndex playerColorIndex,
        const UnitTextureAtlases& atlases,
        float maskValue,
        std::vector<UnitTextureMeshRenderInfo>& batch)
    {
        auto mvpMatrix = viewProjectionMatrix * matrix;

        if (mesh.vertices)
        {
            batch.push_back(UnitTextureMeshRenderInfo{&*mesh.vertices, matrix, mvpMatrix, shadeStrength, atlases.atlas, atlases.paletteIndexAtlas, maskValue});
        }
        if (mesh.teamVertices)
        {
            batch.push_back(UnitTextureMeshRenderInfo{&*mesh.teamVertices, matrix, mvpMatrix, shadeStrength, atlases.teamAtlases->at(playerColorIndex.value).get(), atlases.teamPaletteIndexAtlases->at(playerColorIndex.value).get(), maskValue});
        }
    }

    void drawShaderMeshShadow(
        const Matrix4f& viewProjectionMatrix,
        const ShaderMesh& mesh,
        const Matrix4f& matrix,
        const ShadowProjection& shadow,
        const UnitTextureAtlases& atlases,
        std::vector<UnitTextureShadowMeshRenderInfo>& batch)
    {
        if (mesh.vertices)
        {
            batch.push_back(UnitTextureShadowMeshRenderInfo{&*mesh.vertices, matrix, viewProjectionMatrix, atlases.atlas, shadow.groundHeight, shadow.projected, shadow.originY});
        }
        if (mesh.teamVertices)
        {
            batch.push_back(UnitTextureShadowMeshRenderInfo{&*mesh.teamVertices, matrix, viewProjectionMatrix, atlases.teamAtlases->at(0).get(), shadow.groundHeight, shadow.projected, shadow.originY});
        }
    }

    void drawUnitMesh(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const std::string& objectName,
        const UnitModelDefinition& modelDefinition,
        const std::vector<UnitMesh>& meshes,
        const Matrix4f& modelMatrix,
        PlayerColorIndex playerColorIndex,
        float frac,
        float shadeStrength,
        const UnitTextureAtlases& atlases,
        bool isFinishedBuilding,
        std::vector<UnitTextureMeshRenderInfo>& out)
    {
        const auto& renderInfo = gameMediaDatabase.getUnitModelRenderInfo(objectName, modelDefinition);
        const auto& transforms = computePieceTransformsForRender(modelDefinition, renderInfo, meshes, frac);

        for (Index i = 0; i < getSize(modelDefinition.pieces); ++i)
        {
            const auto& mesh = meshes[i];
            if (!mesh.visible)
            {
                continue;
            }

            // A dont-cache piece is left out of the original's cached bitmap
            // and drawn to the screen by the unshaded rasterizer instead
            // (TOTALA-EXE-SHADING.md S:12a) -- but only once the unit is
            // finished, which is the only time this function draws it; the
            // nanoframe path shades every piece the script has not said
            // DONT_SHADE on, as the construction pass does.
            auto pieceShadeStrength = mesh.shaded && mesh.cached ? shadeStrength : 0.0f;

            // The same flag decides the building halo, for the same reason.
            // The halo is an artefact of the cached bitmap's anti-aliasing, so
            // a piece that is not in that bitmap never met the table that
            // produces it: a metal extractor's spinning top has no fringe in
            // the original while its base does. Everything else solid still
            // goes in at 0.5 as an OCCLUDER -- coverage without being a source
            // -- because a gap in the coverage is a boundary, and the post
            // pass cannot tell a gap from an outline. See unitTexture.frag.
            //
            // A finished building's dont-cache piece is the one in between, at
            // 0.7: no halo, for the reason above, but anti-aliased with the
            // building it belongs to. Left at 0.5 it went sharp or smooth with
            // the units switch, and an extractor's top is part of a building.
            auto maskValue = isFinishedBuilding ? (mesh.cached ? 1.0f : 0.7f) : 0.5f;

            drawShaderMesh(viewProjectionMatrix, *renderInfo.pieces[i]->mesh, modelMatrix * transforms[i], pieceShadeStrength, playerColorIndex, atlases, maskValue, out);
        }
    }

    bool unitCastsShadow(const UnitDefinition& unitDefinition)
    {
        return !unitDefinition.noShadow;
    }

    void drawUnitShadowMesh(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const std::string& objectName,
        const UnitModelDefinition& modelDefinition,
        const std::vector<UnitMesh>& meshes,
        const Matrix4f& modelMatrix,
        float frac,
        const ShadowProjection& shadow,
        const UnitTextureAtlases& atlases,
        UnitShadowMeshBatch& batch)
    {
        const auto& renderInfo = gameMediaDatabase.getUnitModelRenderInfo(objectName, modelDefinition);
        const auto& transforms = computePieceTransformsForRender(modelDefinition, renderInfo, meshes, frac);

        for (Index i = 0; i < getSize(modelDefinition.pieces); ++i)
        {
            const auto& mesh = meshes[i];
            if (!mesh.visible)
            {
                continue;
            }

            drawShaderMeshShadow(viewProjectionMatrix, *renderInfo.pieces[i]->mesh, modelMatrix * transforms[i], shadow, atlases, batch.meshes);
        }
    }

    void drawUnitShadowMeshNoPieces(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const std::string& objectName,
        const UnitModelDefinition& modelDefinition,
        const Matrix4f& modelMatrix,
        const ShadowProjection& shadow,
        const UnitTextureAtlases& atlases,
        UnitShadowMeshBatch& batch)
    {
        const auto& renderInfo = gameMediaDatabase.getUnitModelRenderInfo(objectName, modelDefinition);

        for (Index i = 0; i < getSize(modelDefinition.pieces); ++i)
        {
            drawShaderMeshShadow(viewProjectionMatrix, *renderInfo.pieces[i]->mesh, modelMatrix * renderInfo.restTransforms[i], shadow, atlases, batch.meshes);
        }
    }

    void drawBuildingShaderMesh(
        const Matrix4f& viewProjectionMatrix,
        const ShaderMesh& mesh,
        const Matrix4f& matrix,
        float shadeStrength,
        const BuildPhase& buildPhase,
        float unitY,
        float unitHeight,
        PlayerColorIndex playerColorIndex,
        const UnitTextureAtlases& atlases,
        std::vector<UnitBuildingMeshRenderInfo>& batch)
    {
        auto mvpMatrix = viewProjectionMatrix * matrix;
        if (mesh.vertices)
        {
            batch.push_back(UnitBuildingMeshRenderInfo{&*mesh.vertices, matrix, mvpMatrix, shadeStrength, atlases.atlas, atlases.paletteIndexAtlas, unitY, unitHeight, buildPhase.ratio, buildPhase.aboveMode, buildPhase.bandMode, buildPhase.belowMode, buildPhase.colorA, buildPhase.colorB});
        }
        if (mesh.teamVertices)
        {
            batch.push_back(UnitBuildingMeshRenderInfo{&*mesh.teamVertices, matrix, mvpMatrix, shadeStrength, atlases.teamAtlases->at(playerColorIndex.value).get(), atlases.teamPaletteIndexAtlases->at(playerColorIndex.value).get(), unitY, unitHeight, buildPhase.ratio, buildPhase.aboveMode, buildPhase.bandMode, buildPhase.belowMode, buildPhase.colorA, buildPhase.colorB});
        }
    }

    void drawBuildingUnitMesh(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const std::string& objectName,
        const UnitModelDefinition& modelDefinition,
        const std::vector<UnitMesh>& meshes,
        const Matrix4f& modelMatrix,
        const BuildPhase& buildPhase,
        float unitY,
        PlayerColorIndex playerColorIndex,
        float frac,
        float shadeStrength,
        const UnitTextureAtlases& atlases,
        UnitMeshBatch& batch)
    {
        const auto& renderInfo = gameMediaDatabase.getUnitModelRenderInfo(objectName, modelDefinition);
        const auto& transforms = computePieceTransformsForRender(modelDefinition, renderInfo, meshes, frac);

        for (Index i = 0; i < getSize(modelDefinition.pieces); ++i)
        {
            const auto& mesh = meshes[i];
            if (!mesh.visible)
            {
                continue;
            }

            drawBuildingShaderMesh(viewProjectionMatrix, *renderInfo.pieces[i]->mesh, modelMatrix * transforms[i], mesh.shaded ? shadeStrength : 0.0f, buildPhase, unitY, simScalarToFloat(modelDefinition.height), playerColorIndex, atlases, batch.buildingMeshes);
        }
    }

    void drawProjectileUnitMesh(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const std::string& objectName,
        const UnitModelDefinition& modelDefinition,
        const Matrix4f& modelMatrix,
        PlayerColorIndex playerColorIndex,
        float shadeStrength,
        const UnitTextureAtlases& atlases,
        UnitMeshBatch& batch)
    {
        const auto& renderInfo = gameMediaDatabase.getUnitModelRenderInfo(objectName, modelDefinition);

        for (Index i = 0; i < getSize(modelDefinition.pieces); ++i)
        {
            drawShaderMesh(viewProjectionMatrix, *renderInfo.pieces[i]->mesh, modelMatrix * renderInfo.restTransforms[i], shadeStrength, playerColorIndex, atlases, 0.5f, batch.meshes);
        }
    }

    void drawUnit(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const UnitState& unit,
        const UnitDefinition& unitDefinition,
        const UnitModelDefinition& modelDefinition,
        PlayerColorIndex playerColorIndex,
        unsigned int unitIndex,
        unsigned int gameTime,
        float frac,
        float shadeStrength,
        const UnitTextureAtlases& atlases,
        UnitMeshBatch& batch)
    {
        auto position = lerp(simVectorToFloat(unit.previousPosition), simVectorToFloat(unit.position), frac);
        auto rotation = angleLerp(toRadians(unit.previousRotation).value, toRadians(unit.rotation).value, frac);
        auto transform = unitRenderTransform(unit, unitDefinition, position, rotation, frac);
        if (unit.isBeingBuilt(unitDefinition))
        {
            auto buildPhase = computeBuildPhase(unit.getPreciseCompletePercent(unitDefinition), unitIndex, gameTime);
            drawBuildingUnitMesh(gameMediaDatabase, viewProjectionMatrix, unitDefinition.objectName, modelDefinition, unit.pieces, transform, buildPhase, position.y, playerColorIndex, frac, shadeStrength, atlases, batch);
        }
        else
        {
            // Anything cloaked that got this far belongs to the viewer:
            // computeUnitDrawStyle turned everyone else's away before the unit
            // reached the batch. A nanoframe never gets here cloaked either --
            // the drain skips a unit that is still being built.
            auto& out = unit.cloaked ? batch.cloakedMeshes : batch.meshes;

            // A finished unit whose FBI says ZBuffer=0 gets a cached bitmap
            // with no height plane, and the original's textured span filler
            // only looks the shade table up when there is one: its texels go
            // to the screen untouched (TOTALA-EXE-SHADING.md S:23). Only
            // while it is being built does it get the plane, for the
            // construction wipe, and shade like everything else -- which is
            // why this sits on the finished branch alone. CORFAV and
            // CORTRUCK are the two shipped units it reaches. (The flat-colour
            // n-gons of such a unit are still shaded by the original; RWE's
            // mesh does not keep them apart from the textured quads, so they
            // go unshaded with the rest, a difference on nine faces of the
            // Weasel and one of the truck.)
            auto finishedShadeStrength = unitDefinition.zBuffer ? shadeStrength : 0.0f;
            // Only an immobile unit can carry the building halo. This branch
            // is already the finished one -- a nanoframe went the other way
            // above -- so immobility is the whole of the test, and getting it
            // wrong here puts a purple fringe round every tank and aircraft
            // on the map.
            drawUnitMesh(gameMediaDatabase, viewProjectionMatrix, unitDefinition.objectName, modelDefinition, unit.pieces, transform, playerColorIndex, frac, finishedShadeStrength, atlases, !unitDefinition.isMobile, out);
        }
    }

    void drawMeshFeature(
        const std::unordered_map<std::string, UnitModelDefinition>& modelDefinitions,
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const MapFeature& feature,
        float shadeStrength,
        const UnitTextureAtlases& atlases,
        UnitMeshBatch& batch)
    {
        const auto& featureMediaInfo = gameMediaDatabase.getFeature(feature.featureName);

        if (auto objectInfo = std::get_if<FeatureObjectInfo>(&featureMediaInfo.renderInfo); objectInfo != nullptr)
        {
            const auto& modelDefinition = modelDefinitions.at(objectInfo->objectName);
            auto matrix = Matrix4f::translation(simVectorToFloat(feature.position)) * Matrix4f::rotationY(toRadians(feature.rotation).value);
            drawProjectileUnitMesh(gameMediaDatabase, viewProjectionMatrix, objectInfo->objectName, modelDefinition, matrix, PlayerColorIndex(0), shadeStrength, atlases, batch);
        }
    }

    void drawUnitShadow(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const UnitState& unit,
        const UnitDefinition& unitDefinition,
        const UnitModelDefinition& modelDefinition,
        float frac,
        float groundHeight,
        const UnitTextureAtlases& atlases,
        UnitShadowMeshBatch& batch)
    {
        auto position = lerp(simVectorToFloat(unit.previousPosition), simVectorToFloat(unit.position), frac);
        auto rotation = angleLerp(toRadians(unit.previousRotation).value, toRadians(unit.rotation).value, frac);
        auto transform = unitRenderTransform(unit, unitDefinition, position, rotation, frac);

        // Which of the original's two shadow passes this unit belongs to. It
        // sorts on unit+0x113 bit 5, whose meaning is not established -- "is a
        // building" is the obvious reading, and is what mobility stands in for
        // here (TOTALA-EXE.md S:100).
        //
        // A mobile unit's shadow is a copy of its own silhouette under one
        // displacement, and the displacement is decoded (TOTALA-EXE.md S:100):
        // 0x45933D blits the copy at the unit's screen x plus 0x85 where
        // 0x4597BA draws the unit itself at plus 0x80, and at the screen y of
        // the ground under it rather than of the unit. So five pixels right,
        // and down by however far the unit is above the ground -- nothing, for
        // anything that drives. The height it is taken from is the unit's
        // base. RWE used to take it from the top of the model, which is what
        // made a commander look as if it were floating over its own shadow.
        auto shadow = unitDefinition.isMobile
            ? ShadowProjection{groundHeight, false, position.y}
            : ShadowProjection{groundHeight, true, 0.0f};

        drawUnitShadowMesh(gameMediaDatabase, viewProjectionMatrix, unitDefinition.objectName, modelDefinition, unit.pieces, transform, frac, shadow, atlases, batch);
    }

    void drawFeatureMeshShadow(
        const std::unordered_map<std::string, UnitModelDefinition>& modelDefinitions,
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const MapFeature& feature,
        float groundHeight,
        const UnitTextureAtlases& atlases,
        UnitShadowMeshBatch& batch)
    {
        const auto& featureMediaInfo = gameMediaDatabase.getFeature(feature.featureName);

        auto objectInfo = std::get_if<FeatureObjectInfo>(&featureMediaInfo.renderInfo);
        if (objectInfo == nullptr)
        {
            return;
        }

        const auto& modelDefinition = modelDefinitions.at(objectInfo->objectName);

        const auto& position = feature.position;
        auto matrix = Matrix4f::translation(simVectorToFloat(position)) * Matrix4f::rotationY(toRadians(feature.rotation).value);

        // Scenery never moves, so it takes the projected pass with the buildings.
        drawUnitShadowMeshNoPieces(gameMediaDatabase, viewProjectionMatrix, objectInfo->objectName, modelDefinition, matrix, ShadowProjection{groundHeight, true, 0.0f}, atlases, batch);
    }

    void drawFeature(
        const GameMediaDatabase& gameMediaDatabase,
        const MapFeature& feature,
        const FeatureDefinition& featureDefinition,
        const Matrix4f& viewProjectionMatrix,
        GameTime currentTime,
        bool fogged,
        SpriteBatch& batch)
    {
        const auto& featureMediaInfo = gameMediaDatabase.getFeature(feature.featureName);

        auto spriteInfo = std::get_if<FeatureSpriteInfo>(&featureMediaInfo.renderInfo);
        if (spriteInfo == nullptr)
        {
            return;
        }

        auto position = simVectorToFloat(feature.position);

        // A burning feature shows its burn sequence on a loop instead of its usual frame.
        const SpriteSeries* series = spriteInfo->animation.get();
        std::size_t frameIndex = 0;
        if (feature.burningUntil && spriteInfo->burnAnimation && !(*spriteInfo->burnAnimation)->sprites.empty())
        {
            series = spriteInfo->burnAnimation->get();
            frameIndex = (currentTime.value / 2) % series->sprites.size();
        }
        const auto& sprite = *series->sprites[frameIndex];

        Vector3f snappedPosition(std::round(position.x), truncateToInterval(position.y, 2.0f), std::round(position.z));

        if (featureDefinition.isStanding())
        {
            // A standing sprite is a vertical wall whose every pixel occludes
            // at the wall's own z. Anchored at the footprint centre, a wide
            // crystal's artwork cut through buildings standing beside it --
            // the whole north half of an adjacent structure vanished under
            // the crystal's skirt. Anchor the wall just inside the footprint's
            // north edge instead: shifting (y, z) by (-2d, -d) leaves every
            // pixel's screen position unchanged (screenY = z - y/2) while
            // moving only the depth plane, since depth under this camera is
            // world y alone.
            float depthShift = (static_cast<float>(featureDefinition.footprintZ) * 8.0f) - 0.5f;
            snappedPosition.z -= depthShift;
            snappedPosition.y -= 2.0f * depthShift;
        }

        // Convert to a model position that makes sense in the game world.
        // For standing (blocking) features we stretch y-dimension values by 2x
        // to correct for TA camera distortion.
        Matrix4f conversionMatrix = featureDefinition.isStanding()
            ? Matrix4f::scale(Vector3f(1.0f, -2.0f, 1.0f))
            : Matrix4f::rotationX(-Pif / 2.0f) * Matrix4f::scale(Vector3f(1.0f, -1.0f, 1.0f));

        auto modelMatrix = Matrix4f::translation(snappedPosition) * conversionMatrix * sprite.getTransform();

        auto mvpMatrix = viewProjectionMatrix * modelMatrix;

        batch.sprites.push_back(SpriteRenderInfo{&sprite, mvpMatrix, spriteInfo->transparentAnimation, fogged});
    }

    void drawFeatureShadow(
        const GameMediaDatabase& gameMediaDatabase,
        const MapFeature& feature,
        const FeatureDefinition& featureDefinition,
        const Matrix4f& viewProjectionMatrix,
        bool fogged,
        SpriteBatch& batch)
    {
        const auto& featureMediaInfo = gameMediaDatabase.getFeature(feature.featureName);

        auto spriteInfo = std::get_if<FeatureSpriteInfo>(&featureMediaInfo.renderInfo);
        if (spriteInfo == nullptr)
        {
            return;
        }

        if (!spriteInfo->shadowAnimation)
        {
            return;
        }
        auto position = simVectorToFloat(feature.position);
        const auto& sprite = *(*spriteInfo->shadowAnimation)->sprites[0];

        Vector3f snappedPosition(std::round(position.x), truncateToInterval(position.y, 2.0f), std::round(position.z));

        if (featureDefinition.isStanding())
        {
            // A standing sprite is a vertical wall whose every pixel occludes
            // at the wall's own z. Anchored at the footprint centre, a wide
            // crystal's artwork cut through buildings standing beside it --
            // the whole north half of an adjacent structure vanished under
            // the crystal's skirt. Anchor the wall just inside the footprint's
            // north edge instead: shifting (y, z) by (-2d, -d) leaves every
            // pixel's screen position unchanged (screenY = z - y/2) while
            // moving only the depth plane, since depth under this camera is
            // world y alone.
            float depthShift = (static_cast<float>(featureDefinition.footprintZ) * 8.0f) - 0.5f;
            snappedPosition.z -= depthShift;
            snappedPosition.y -= 2.0f * depthShift;
        }

        // Convert to a model position that makes sense in the game world.
        // For standing (blocking) features we stretch y-dimension values by 2x
        // to correct for TA camera distortion.
        Matrix4f conversionMatrix = featureDefinition.isStanding()
            ? Matrix4f::scale(Vector3f(1.0f, -2.0f, 1.0f))
            : Matrix4f::rotationX(-Pif / 2.0f) * Matrix4f::scale(Vector3f(1.0f, -1.0f, 1.0f));

        auto modelMatrix = Matrix4f::translation(snappedPosition) * conversionMatrix * sprite.getTransform();

        auto mvpMatrix = viewProjectionMatrix * modelMatrix;

        batch.sprites.push_back(SpriteRenderInfo{&sprite, mvpMatrix, spriteInfo->transparentShadow, fogged});
    }

    void drawDebrisPiece(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const std::string& objectName,
        const std::string& pieceName,
        const Matrix4f& matrix,
        PlayerColorIndex playerColorIndex,
        float shadeStrength,
        const UnitTextureAtlases& atlases,
        UnitMeshBatch& batch)
    {
        auto pieceMesh = gameMediaDatabase.getUnitPieceMesh(objectName, pieceName);
        if (!pieceMesh)
        {
            return;
        }
        drawShaderMesh(viewProjectionMatrix, *pieceMesh->get().mesh, matrix, shadeStrength, playerColorIndex, atlases, 0.5f, batch.meshes);
    }

    void drawDebrisShard(const Vector3f& position, ColoredMeshBatch& batch)
    {
        const Vector3f color(0.22f, 0.2f, 0.18f);
        const auto topLeft = position + Vector3f(-1.5f, 0.0f, -1.5f);
        const auto topRight = position + Vector3f(1.5f, 0.0f, -1.5f);
        const auto bottomLeft = position + Vector3f(-1.5f, 0.0f, 1.5f);
        const auto bottomRight = position + Vector3f(1.5f, 0.0f, 1.5f);
        pushTriangle(batch.triangles, topLeft, bottomLeft, bottomRight, color);
        pushTriangle(batch.triangles, topLeft, bottomRight, topRight, color);
    }

    namespace
    {
        /** How far, in world units, a wireframe or selection pixel is lifted towards the camera. */
        constexpr float WireframeDepthLift = 2.0f;

        /** Where a world-space point lands on screen, in output pixels, y down. */
        Vector2f toScreenPixels(const WireframeScreen& screen, const Vector3f& world)
        {
            auto clip = screen.viewProjection * world;
            return Vector2f((clip.x + 1.0f) * 0.5f * screen.width, (1.0f - clip.y) * 0.5f * screen.height);
        }

        /**
         * One output pixel of wireframe, centred on centre. The camera is
         * orthographic, so a pixel's width and height are the same two
         * world-space steps everywhere on screen.
         */
        void pushWireframePixel(const WireframeScreen& screen, const Vector3f& centre, const Vector3f& color, std::vector<GlColoredVertex>& out)
        {
            auto right = screen.pixelRight * 0.5f;
            auto up = screen.pixelUp * 0.5f;
            auto bottomLeft = centre - right - up;
            auto bottomRight = centre + right - up;
            auto topRight = centre + right + up;
            auto topLeft = centre - right + up;

            // Anticlockwise on screen, which is the way culling keeps.
            pushTriangle(out, bottomLeft, bottomRight, topRight, color);
            pushTriangle(out, bottomLeft, topRight, topLeft, color);
        }
    }

    void drawUnitWireframe(
        const GameMediaDatabase& gameMediaDatabase,
        const UnitState& unit,
        const UnitDefinition& unitDefinition,
        const UnitModelDefinition& modelDefinition,
        float frac,
        const Vector3f& toCamera,
        const WireframeScreen& screen,
        const Vector3f& color,
        ColoredMeshBatch& batch)
    {
        assert(modelDefinition.pieces.size() == unit.pieces.size());

        auto position = lerp(simVectorToFloat(unit.previousPosition), simVectorToFloat(unit.position), frac);
        auto rotation = angleLerp(toRadians(unit.previousRotation).value, toRadians(unit.rotation).value, frac);
        auto transform = unitRenderTransform(unit, unitDefinition, position, rotation, frac);

        // Lift each pixel towards the camera so it passes the depth test
        // against the surface it lies on, while anything the model itself
        // hides stays hidden. That stands in for the original's height test,
        // which keeps a wireframe pixel only where no higher surface of the
        // model covers it (0x4C0A90). The pixel is a flat square facing the
        // camera, and a surface seen at a slant falls away across it, so the
        // lift has to clear that slope as well as the surface: at 0.75 part
        // of a pixel could sink behind the face it outlines, which reads as a
        // thinner, broken line.
        auto bias = toCamera * WireframeDepthLift;

        const auto& renderInfo = gameMediaDatabase.getUnitModelRenderInfo(unitDefinition.objectName, modelDefinition);
        const auto& transforms = computePieceTransformsForRender(modelDefinition, renderInfo, unit.pieces, frac);

        std::vector<Vector3f> world;
        std::vector<Vector2f> onScreen;
        std::vector<WireframePixel> pixels;
        for (Index i = 0; i < getSize(modelDefinition.pieces); ++i)
        {
            if (!unit.pieces[i].visible)
            {
                continue;
            }

            auto matrix = transform * transforms[i];
            const auto& pieceInfo = *renderInfo.pieces[i];
            if (!pieceInfo.polygons)
            {
                continue;
            }

            // Every polygon goes through the scan conversion, and it is the
            // conversion that drops the ones facing away (TOTALA-EXE.md S:3).
            for (const auto& polygon : *pieceInfo.polygons)
            {
                world.clear();
                onScreen.clear();
                for (const auto& corner : polygon.vertices)
                {
                    auto w = matrix * corner;
                    world.push_back(w);
                    onScreen.push_back(toScreenPixels(screen, w));
                }

                pixels.clear();
                scanWireframePolygon(onScreen, pixels);
                for (const auto& p : pixels)
                {
                    // The point on the edge this pixel came from, moved across
                    // the screen to the pixel's centre. The move is in the
                    // image plane, so the depth stays the polygon's.
                    auto onEdge = lerp(world[p.from], world[p.to], p.t);
                    auto centre = onEdge + (screen.pixelRight * ((static_cast<float>(p.x) + 0.5f) - p.edgeX)) + bias;
                    pushWireframePixel(screen, centre, color, batch.triangles);
                }
            }
        }
    }

    /**
     * Palette entries 161..167, the greens the original sprays. Each particle
     * walks this list one step per tick and wraps round, so the stream
     * shimmers from pale to dark along its length rather than being dyed one
     * colour per particle.
     */
    /**
     * Palette entries 97..103, the seven water blues a wake walks up from
     * palest to deepest. Read out of the shipped PALETTE.PAL.
     */
    static const std::array<Vector3f, 7> WakeColors{
        Vector3f(203 / 255.0f, 227 / 255.0f, 255 / 255.0f),
        Vector3f(175 / 255.0f, 207 / 255.0f, 255 / 255.0f),
        Vector3f(151 / 255.0f, 179 / 255.0f, 255 / 255.0f),
        Vector3f(123 / 255.0f, 151 / 255.0f, 255 / 255.0f),
        Vector3f(103 / 255.0f, 127 / 255.0f, 255 / 255.0f),
        Vector3f(83 / 255.0f, 107 / 255.0f, 239 / 255.0f),
        Vector3f(63 / 255.0f, 91 / 255.0f, 227 / 255.0f),
    };

    static const std::array<Vector3f, 7> NanoSprayColors{
        Vector3f(171 / 255.0f, 231 / 255.0f, 127 / 255.0f),
        Vector3f(131 / 255.0f, 211 / 255.0f, 91 / 255.0f),
        Vector3f(103 / 255.0f, 191 / 255.0f, 63 / 255.0f),
        Vector3f(75 / 255.0f, 171 / 255.0f, 43 / 255.0f),
        Vector3f(67 / 255.0f, 151 / 255.0f, 43 / 255.0f),
        Vector3f(55 / 255.0f, 135 / 255.0f, 39 / 255.0f),
        Vector3f(47 / 255.0f, 119 / 255.0f, 27 / 255.0f),
    };

    /**
     * Palette entries 160..175: the sixteen greens, palest to near black, that
     * the original's construction display cycles through.
     */
    static const std::array<Vector3f, 16> BuildCycleColors{
        Vector3f(215 / 255.0f, 255 / 255.0f, 167 / 255.0f),
        Vector3f(171 / 255.0f, 231 / 255.0f, 127 / 255.0f),
        Vector3f(131 / 255.0f, 211 / 255.0f, 91 / 255.0f),
        Vector3f(103 / 255.0f, 191 / 255.0f, 63 / 255.0f),
        Vector3f(75 / 255.0f, 171 / 255.0f, 43 / 255.0f),
        Vector3f(67 / 255.0f, 151 / 255.0f, 43 / 255.0f),
        Vector3f(55 / 255.0f, 135 / 255.0f, 39 / 255.0f),
        Vector3f(47 / 255.0f, 119 / 255.0f, 27 / 255.0f),
        Vector3f(43 / 255.0f, 103 / 255.0f, 19 / 255.0f),
        Vector3f(35 / 255.0f, 91 / 255.0f, 15 / 255.0f),
        Vector3f(31 / 255.0f, 79 / 255.0f, 11 / 255.0f),
        Vector3f(27 / 255.0f, 67 / 255.0f, 7 / 255.0f),
        Vector3f(23 / 255.0f, 51 / 255.0f, 0 / 255.0f),
        Vector3f(15 / 255.0f, 39 / 255.0f, 0 / 255.0f),
        Vector3f(11 / 255.0f, 27 / 255.0f, 0 / 255.0f),
        Vector3f(7 / 255.0f, 15 / 255.0f, 0 / 255.0f),
    };

    Vector3f buildCycleColor(unsigned int unitIndex, unsigned int gameTime, unsigned int xorKey, unsigned int rate)
    {
        // A counter that advances rate/30 places a tick, offset per unit so
        // that two things being built side by side are not in step, then
        // folded back on itself every sixteen places to give a triangle wave
        // down the greens and back up again.
        auto n = (unitIndex ^ xorKey) + ((rate * gameTime) / 30);
        auto low = n & 0x0Fu;
        return BuildCycleColors[(n & 0x10u) ? (15u - low) : low];
    }

    Vector3f buildCycleColorA(unsigned int unitIndex, unsigned int gameTime)
    {
        return buildCycleColor(unitIndex, gameTime, 5, 33);
    }

    Vector3f buildCycleColorB(unsigned int unitIndex, unsigned int gameTime)
    {
        return buildCycleColor(unitIndex, gameTime, 9, 57);
    }

    BuildPhase computeBuildPhase(float percentComplete, unsigned int unitIndex, unsigned int gameTime)
    {
        // The original tracks the fraction still to build, scaled to a byte,
        // and switches the display at five thresholds on it.
        auto v = std::clamp(static_cast<int>((1.0f - percentComplete) * 255.0f), 0, 255);

        BuildPhase phase;
        phase.colorA = buildCycleColorA(unitIndex, gameTime);
        phase.colorB = buildCycleColorB(unitIndex, gameTime);

        int ratio;
        if (v >= 236)
        {
            // Nothing but a single line sweeping down the model.
            ratio = 255 * (v - 235) / 20;
            phase.aboveMode = BuildFillMode::Erase;
            phase.bandMode = BuildFillMode::ColorA;
            phase.belowMode = BuildFillMode::Erase;
        }
        else if (v >= 201)
        {
            // A second, slower sweep down.
            ratio = 255 * (v - 200) / 35;
            phase.aboveMode = BuildFillMode::Erase;
            phase.bandMode = BuildFillMode::ColorA;
            phase.belowMode = BuildFillMode::Erase;
        }
        else if (v >= 116)
        {
            // The solid green silhouette grows up from the base.
            ratio = (3 * (115 - v)) - 1;
            phase.aboveMode = BuildFillMode::Erase;
            phase.bandMode = BuildFillMode::ColorB;
            phase.belowMode = BuildFillMode::ColorA;
        }
        else if (v >= 31)
        {
            // The texture follows it up, green still above the line.
            ratio = (3 * (30 - v)) - 1;
            phase.aboveMode = BuildFillMode::ColorA;
            phase.bandMode = BuildFillMode::ColorB;
            phase.belowMode = BuildFillMode::Texture;
        }
        else
        {
            // One last line sweeps back down over the finished texture.
            ratio = 255 * v / 30;
            phase.aboveMode = BuildFillMode::Texture;
            phase.bandMode = BuildFillMode::ColorA;
            phase.belowMode = BuildFillMode::Texture;
        }

        // Only the low byte of the threshold reaches the original's remapper,
        // which is why the two middle phases can compute a negative number and
        // still sweep upwards.
        phase.ratio = static_cast<float>(ratio & 0xFF);
        return phase;
    }

    void drawNanoParticle(GameTime currentTime, float frac, const Particle& particle, ColoredMeshBatch& batch)
    {
        auto nanoRenderInfo = std::get_if<ParticleRenderTypeNano>(&particle.renderType);
        if (nanoRenderInfo == nullptr)
        {
            return;
        }

        if (!particle.isStarted(currentTime) || currentTime >= nanoRenderInfo->finishTime)
        {
            return;
        }

        // One step along the cycle per tick since the particle was spawned.
        auto age = currentTime.value - particle.startTime.value;
        const auto& color = NanoSprayColors[(nanoRenderInfo->colorPhase + age) % NanoSprayColors.size()];

        // Particles step once per tick; draw them part-way along this tick's
        // step so the stream flows at the frame rate rather than at 30 Hz.
        auto position = particle.position + (particle.velocity * frac);

        // Nudge towards the camera, where asked, so a structure cannot swallow
        // the spray being poured into it. The world camera maps depth to world
        // Y and screen height to (0.5 * y - z), so raising y by d and z by d/2
        // moves the quad forward in the depth buffer without shifting it on
        // screen by a single pixel.
        auto nudge = nanoRenderInfo->depthNudge;
        position = position + Vector3f(0.0f, nudge, nudge / 2.0f);

        // A flat square: the camera looks straight down (with a cabinet skew
        // for height), so this reads as a screen-aligned pixel block.
        auto s = nanoRenderInfo->halfSize;
        const auto topLeft = position + Vector3f(-s, 0.0f, -s);
        const auto topRight = position + Vector3f(s, 0.0f, -s);
        const auto bottomLeft = position + Vector3f(-s, 0.0f, s);
        const auto bottomRight = position + Vector3f(s, 0.0f, s);

        pushTriangle(batch.triangles, topLeft, bottomLeft, bottomRight, color);
        pushTriangle(batch.triangles, topLeft, bottomRight, topRight, color);
    }

    void drawWakeParticle(const GameMediaDatabase& gameMediaDatabase, GameTime currentTime, const Matrix4f& viewProjectionMatrix, const Particle& particle, ColoredMeshBatch& batch)
    {
        auto wakeRenderInfo = std::get_if<ParticleRenderTypeWake>(&particle.renderType);
        if (wakeRenderInfo == nullptr)
        {
            return;
        }

        if (!particle.isStarted(currentTime) || currentTime >= wakeRenderInfo->finishTime)
        {
            return;
        }

        // Wake dots are the longest list the renderer walks: a wake lives
        // ninety-six ticks and every hovercraft lays two a tick, so eight
        // hundred of them keep about 130,000 dots alive between them and the
        // camera can see a few hundred. Six vertices each is eighteen
        // megabytes of vertex buffer a frame if all of them go in, so the
        // ones outside the view are dropped here rather than uploaded for the
        // GPU to clip. The projection is orthographic, so a point transforms
        // straight to clip space with no divide, and the dot is two world
        // units across -- far inside the margin.
        auto clipPosition = viewProjectionMatrix * particle.position;
        if (clipPosition.x < -1.05f || clipPosition.x > 1.05f || clipPosition.y < -1.05f || clipPosition.y > 1.05f)
        {
            return;
        }

        const auto topLeft = particle.position + Vector3f(-1.0f, 0.0f, -1.0f);
        const auto topRight = particle.position + Vector3f(1.0f, 0.0f, -1.0f);
        const auto bottomLeft = particle.position + Vector3f(-1.0f, 0.0f, 1.0f);
        const auto bottomRight = particle.position + Vector3f(1.0f, 0.0f, 1.0f);

        // A discrete step along the seven water blues rather than a fade. The
        // original advances the palette index by one every rampPeriod ticks
        // and never wraps, because the life is exactly six steps long.
        auto age = static_cast<unsigned int>((currentTime - particle.startTime).value);
        const auto& color = WakeColors[wakeColorIndex(age, wakeRenderInfo->rampPeriod)];

        pushTriangle(batch.triangles, topLeft, bottomLeft, bottomRight, color);
        pushTriangle(batch.triangles, topLeft, bottomRight, topRight, color);
    }

    void drawSpriteParticle(const GameMediaDatabase& gameMediaDatabase, GameTime currentTime, const Matrix4f& viewProjectionMatrix, const Particle& particle, SpriteBatch& batch)
    {
        auto spriteRenderInfo = std::get_if<ParticleRenderTypeSprite>(&particle.renderType);
        if (spriteRenderInfo == nullptr)
        {
            return;
        }

        auto spriteSeries = gameMediaDatabase.getSpriteSeries(spriteRenderInfo->gafName, spriteRenderInfo->animName).value();

        if (!particle.isStarted(currentTime) || particle.isFinished(currentTime, *spriteRenderInfo, spriteSeries->sprites.size()))
        {
            return;
        }

        auto frameIndex = particle.getFrameIndex(currentTime, *spriteRenderInfo, spriteSeries->sprites.size());
        const auto& sprite = *spriteSeries->sprites[frameIndex];

        Vector3f snappedPosition(
            std::round(particle.position.x),
            truncateToInterval(particle.position.y, 2.0f),
            std::round(particle.position.z));

        // Convert to a model position that makes sense in the game world.
        // For standing (blocking) features we stretch y-dimension values by 2x
        // to correct for TA camera distortion.
        Matrix4f conversionMatrix = Matrix4f::scale(Vector3f(1.0f, -2.0f, 1.0f));

        auto modelMatrix = Matrix4f::translation(snappedPosition) * conversionMatrix * sprite.getTransform();
        auto mvpMatrix = viewProjectionMatrix * modelMatrix;

        batch.sprites.push_back(SpriteRenderInfo{&sprite, mvpMatrix, spriteRenderInfo->translucent});
    }

    namespace
    {
        /**
         * True when the ground under (x, z) is certainly below sea level,
         * settled from the heightmap's corner values alone.
         *
         * MapTerrain::getHeightAt casts a vertical ray and intersects it with
         * the four triangles a heightmap cell is split into -- and with those
         * of the neighbouring cells as well, to close the cracks floating
         * point leaves along a shared edge. That is up to sixteen ray-triangle
         * tests. updateParticles calls it once per wake dot per tick, and a
         * wake lives ninety-six ticks while every hovercraft lays two a tick,
         * so eight hundred of them had it running 130,000 times a tick: ten
         * milliseconds, the largest single cost in the frame.
         *
         * Every vertex of every one of those triangles is either a cell corner
         * or a cell centre, and a cell centre is the average of its four
         * corners, so every height the exact query can return lies between the
         * lowest and the highest corner of the cells it looks at. When the
         * highest of them is still under water the answer cannot be anything
         * else and the ray need not be cast. A wake dot spends nearly all of
         * its life over open water, where this settles it with sixteen byte
         * comparisons; along a shore, where it does not settle it, the exact
         * query still runs and still decides.
         */
        bool groundIsCertainlyBelowSeaLevel(const MapTerrain& terrain, float x, float z)
        {
            const auto& heights = terrain.getHeightMap();
            auto cell = terrain.worldToHeightmapCoordinate(SimVector(SimScalar(x), 0_ss, SimScalar(z)));

            // The exact query may walk into any of the eight cells around
            // this one, whose corners span the four-by-four block below.
            // Anywhere near the edge of the map, leave it to the query: it
            // has an out-of-bounds answer of its own.
            if (cell.x < 1 || cell.y < 1 || cell.x + 2 >= heights.getWidth() || cell.y + 2 >= heights.getHeight())
            {
                return false;
            }

            unsigned char highest = 0;
            for (int dy = -1; dy <= 2; ++dy)
            {
                for (int dx = -1; dx <= 2; ++dx)
                {
                    highest = std::max(highest, heights.get(cell.x + dx, cell.y + dy));
                }
            }

            return SimScalar(highest) < terrain.getSeaLevel();
        }
    }

    void updateParticles(const GameMediaDatabase& gameMediaDatabase, const MapTerrain& terrain, GameTime currentTime, std::vector<Particle>& particles)
    {
        auto end = particles.end();
        for (auto it = particles.begin(); it != end;)
        {
            auto& particle = *it;
            auto isFinished = match(
                particle.renderType,
                [&](const ParticleRenderTypeSprite& s) {
                    const auto anim = gameMediaDatabase.getSpriteSeries(s.gafName, s.animName).value();
                    return particle.isFinished(currentTime, s, anim->sprites.size());
                },
                [&](const ParticleRenderTypeWake& w) {
                    if (currentTime >= w.finishTime)
                    {
                        return true;
                    }

                    // The original kills a wake dot the moment the ground
                    // under it comes up to sea level, which is what makes a
                    // wake stop cleanly at a shoreline instead of running up
                    // the beach behind the ship.
                    if (groundIsCertainlyBelowSeaLevel(terrain, particle.position.x, particle.position.z))
                    {
                        return false;
                    }

                    auto groundHeight = terrain.getHeightAt(
                        SimScalar(particle.position.x),
                        SimScalar(particle.position.z));
                    return groundHeight >= terrain.getSeaLevel();
                },
                [&](const ParticleRenderTypeNano& n) {
                    return currentTime >= n.finishTime;
                });

            if (isFinished)
            {
                particle = std::move(*--end);
                continue;
            }

            particle.position += particle.velocity;

            ++it;
        }
        particles.erase(end, particles.end());
    }

    std::vector<Vector3f> findGeoVentSteamPoints(const GameSimulation& simulation)
    {
        std::vector<Vector3f> points;

        for (const auto& [_, feature] : simulation.features)
        {
            if (!simulation.getFeatureDefinition(feature.featureName).geothermal)
            {
                continue;
            }

            points.push_back(simVectorToFloat(feature.position));
        }

        return points;
    }

    /**
     * Assumes input vector is normalised.
     */
    Matrix4f pointDirection(const Vector3f& direction)
    {
        auto right = direction.cross(Vector3f(0.0f, 1.0f, 0.0f)).normalizedOr(Vector3f(1.0f, 0.0f, 0.0f));
        auto realUp = right.cross(direction);
        return Matrix4f::rotationToAxes(right, realUp, direction);
    }

    Matrix4f rotationModeToMatrix(ProjectileRenderTypeModel::RotationMode r)
    {
        switch (r)
        {
            case ProjectileRenderTypeModel::RotationMode::HalfZ:
                return Matrix4f::rotationZ(0.0f, -1.0f); // 180 degrees
            case ProjectileRenderTypeModel::RotationMode::QuarterY:
                return Matrix4f::rotationY(1.0f, 0.0f); // 90 degrees
            case ProjectileRenderTypeModel::RotationMode::None:
                return Matrix4f::identity();
            default:
                throw std::logic_error("Unknown RotationMode");
        }
    }

    unsigned int getFrameIndex(GameTime currentTime, unsigned int numFrames)
    {
        return (currentTime.value / 2) % numFrames;
    }

    void drawProjectiles(
        const GameSimulation& sim,
        const PlayerVisibility& visibility,
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const VectorMap<Projectile, ProjectileIdTag>& projectiles,
        GameTime currentTime,
        float frac,
        const UnitTextureAtlases& atlases,
        ColoredMeshBatch& coloredMeshbatch,
        SpriteBatch& spriteBatch,
        UnitMeshBatch& unitMeshBatch)
    {
        for (const auto& e : projectiles)
        {
            const auto& projectile = e.second;

            // Where it is now, not where it is drawn: the interpolated
            // position is a fraction of a tick's travel away and would put a
            // round on the wrong side of a cell boundary for a frame.
            if (!effectIsVisibleToPlayer(sim, visibility, projectile.position))
            {
                continue;
            }

            auto position = lerp(simVectorToFloat(projectile.previousPosition), simVectorToFloat(projectile.position), frac);

            const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(projectile.weaponType);

            match(
                weaponMediaInfo.renderType,
                [&](const ProjectileRenderTypeLaser& l) {
                    Vector3f pixelOffset(0.0f, 0.0f, -1.0f);

                    std::vector<GlColoredVertex> laserVertices;
                    auto backPosition = lerp(simVectorToFloat(projectile.getPreviousBackPosition(l.duration)), simVectorToFloat(projectile.getBackPosition(l.duration)), frac);

                    coloredMeshbatch.lines.emplace_back(position, l.color);
                    coloredMeshbatch.lines.emplace_back(backPosition, l.color);

                    coloredMeshbatch.lines.emplace_back(position + pixelOffset, l.color2);
                    coloredMeshbatch.lines.emplace_back(backPosition + pixelOffset, l.color2);
                },
                [&](const ProjectileRenderTypeModel& m) {
                    // A missile flies exactly where its nose points (0x49BA74
                    // rebuilds the velocity out of the attitude every tick),
                    // so the velocity vector is the model's heading -- with
                    // two exceptions where it is not a direction at all.
                    // Bombs dropped by a hovering aircraft spawn at rest and
                    // wait a tick for gravity, and a vertical launch spawns at
                    // rest by definition: `startvelocity` is absent from every
                    // vlaunch weapon in the game, so a nuke's first frame has
                    // a zero vector where its heading should be. Falling back
                    // to due north drew the missile lying flat across the pad
                    // for that frame before it snapped upright. The attitude
                    // is right from the moment it is spawned, so use that.
                    auto velocity = simVectorToFloat(projectile.velocity);
                    auto direction = velocity.lengthSquared() > 0.0f
                        ? velocity.normalized()
                        : simVectorToFloat(toMissileDirection(projectile.heading, projectile.pitch));
                    auto transform = Matrix4f::translation(position)
                        * pointDirection(direction)
                        * rotationModeToMatrix(m.rotationMode);
                    const auto& modelDefinition = sim.unitModelDefinitions.at(m.objectName);
                    drawProjectileUnitMesh(gameMediaDatabase, viewProjectionMatrix, m.objectName, modelDefinition, transform, PlayerColorIndex(0), 0.0f, atlases, unitMeshBatch);
                },
                [&](const ProjectileRenderTypeSprite& s) {
                    Vector3f snappedPosition(
                        std::round(position.x),
                        truncateToInterval(position.y, 2.0f),
                        std::round(position.z));
                    Matrix4f conversionMatrix = Matrix4f::scale(Vector3f(1.0f, -2.0f, 1.0f));
                    const auto spriteSeries = gameMediaDatabase.getSpriteSeries(s.gaf, s.anim).value();
                    const auto& sprite = *spriteSeries->sprites[getFrameIndex(currentTime, spriteSeries->sprites.size())];
                    auto modelMatrix = Matrix4f::translation(snappedPosition) * conversionMatrix * sprite.getTransform();
                    auto mvpMatrix = viewProjectionMatrix * modelMatrix;
                    spriteBatch.sprites.push_back(SpriteRenderInfo{&sprite, mvpMatrix, false});
                },
                [&](const ProjectileRenderTypeFlamethrower&) {
                    Vector3f snappedPosition(
                        std::round(position.x),
                        truncateToInterval(position.y, 2.0f),
                        std::round(position.z));
                    Matrix4f conversionMatrix = Matrix4f::scale(Vector3f(1.0f, -2.0f, 1.0f));
                    const auto spriteSeries = gameMediaDatabase.getSpriteSeries("FX", "flamestream").value();
                    auto timeSinceSpawn = currentTime - projectile.createdAt;
                    auto fullLifetime = projectile.dieOnFrame.value() - projectile.createdAt;
                    auto percentComplete = static_cast<float>(timeSinceSpawn.value) / static_cast<float>(fullLifetime.value);
                    auto frameIndex = static_cast<unsigned int>(percentComplete * spriteSeries->sprites.size());
                    assert(frameIndex < spriteSeries->sprites.size());
                    const auto& sprite = *spriteSeries->sprites[frameIndex];
                    auto modelMatrix = Matrix4f::translation(snappedPosition) * conversionMatrix * sprite.getTransform();
                    auto mvpMatrix = viewProjectionMatrix * modelMatrix;
                    spriteBatch.sprites.push_back(SpriteRenderInfo{&sprite, mvpMatrix, true});
                },
                [&](const ProjectileRenderTypeLightning& l) {
                    Vector3f pixelOffset(0.0f, 0.0f, -1.0f);

                    std::vector<GlColoredVertex> laserVertices;
                    auto backPosition = lerp(simVectorToFloat(projectile.getPreviousBackPosition(l.duration)), simVectorToFloat(projectile.getBackPosition(l.duration)), frac);

                    coloredMeshbatch.lines.emplace_back(position, l.color);
                    coloredMeshbatch.lines.emplace_back(backPosition, l.color);
                },
                [&](const ProjectileRenderTypeMindgun&) {
                    // TODO: implement mindgun if anyone actually uses it
                },
                [&](const ProjectileRenderTypeNone&) {
                    // do nothing
                });
        }
    }

    void drawSelectionRect(const GameMediaDatabase& gameMediaDatabase, const WireframeScreen& screen, const Vector3f& toCamera, const UnitState& unit, const UnitDefinition& unitDefinition, float frac, ColoredMeshBatch& batch)
    {
        auto quad = gameMediaDatabase.getSelectionQuad(unitDefinition.objectName);
        if (!quad)
        {
            return;
        }

        auto position = lerp(simVectorToFloat(unit.previousPosition), simVectorToFloat(unit.position), frac);
        auto rotation = angleLerp(toRadians(unit.previousRotation).value, toRadians(unit.rotation).value, frac);
        auto matrix = Matrix4f::translation(position) * Matrix4f::rotationY(rotation);

        // The selection plate's outline in green, one output pixel wide and
        // solid. It was a GL line loop, which the double-size world buffer
        // made half a pixel wide, and where the ground sat under a line the
        // resolve's one sample of each block could miss it altogether.
        const Vector3f color(0.325f, 0.875f, 0.310f);
        auto bias = toCamera * WireframeDepthLift;

        std::array<Vector3f, 4> world;
        std::array<Vector2f, 4> onScreen;
        for (std::size_t i = 0; i < 4; ++i)
        {
            world[i] = matrix * (*quad)[i];
            onScreen[i] = toScreenPixels(screen, world[i]);
        }

        std::vector<LinePixel> pixels;
        for (std::size_t i = 0; i < 4; ++i)
        {
            auto j = (i + 1) % 4;
            pixels.clear();
            scanLine(onScreen[i], onScreen[j], pixels);
            for (const auto& p : pixels)
            {
                // The point on the line this pixel came from, moved in the
                // image plane to the pixel's centre so its depth is kept.
                auto onLine = lerp(world[i], world[j], p.t);
                auto exactX = onScreen[i].x + (p.t * (onScreen[j].x - onScreen[i].x));
                auto exactY = onScreen[i].y + (p.t * (onScreen[j].y - onScreen[i].y));
                auto centre = onLine
                    + (screen.pixelRight * ((static_cast<float>(p.x) + 0.5f) - exactX))
                    - (screen.pixelUp * ((static_cast<float>(p.y) + 0.5f) - exactY))
                    + bias;
                pushWireframePixel(screen, centre, color, batch.triangles);
            }
        }
    }

    WakeEmission computeWakeEmission(const Vector3f& firstVertex, const Vector3f& secondVertex, bool reverse, unsigned int rampPeriod)
    {
        // Reverse swaps which end the foam comes off, and with it the drift.
        const auto& spawnPosition = reverse ? secondVertex : firstVertex;
        const auto& otherVertex = reverse ? firstVertex : secondVertex;

        // Half a world unit a tick, whatever the two vertices are: the
        // original normalises the difference before scaling it, so how far
        // apart the modeller put them makes no difference to the speed.
        auto offset = otherVertex - spawnPosition;
        auto velocity = offset.lengthSquared() > 0.0f ? offset.normalized() / 2.0f : Vector3f(0.0f, 0.0f, 0.0f);

        // Six colour steps, so the dot reaches the last blue as it dies.
        return WakeEmission{spawnPosition, velocity, GameTime(rampPeriod * 6)};
    }

    std::size_t wakeColorIndex(unsigned int age, unsigned int rampPeriod)
    {
        if (rampPeriod == 0)
        {
            return 0;
        }

        return std::min<std::size_t>(age / rampPeriod, WakeColors.size() - 1);
    }

    void accumulateScreenShake(ScreenShakeState& state, int magnitude, int durationTicks)
    {
        // A shake that is not already running starts from nothing. The
        // original clears only the two magnitudes here and leaves the
        // durations to be averaged in below, which is why a fresh shake ends
        // up with half the duration its weapon asked for unless the previous
        // one happened to leave the same number behind.
        if (!state.running)
        {
            state.magnitudeX = 0;
            state.magnitudeY = 0;
        }

        // Note that the running total is read here without being cleared even
        // for a shake that is not running, so a fresh shake is averaged
        // against whatever the last one left behind. That is what 0x41C67F
        // does and it is left alone.
        state.totalTicks = (durationTicks + state.totalTicks) / 2;
        state.ticksRemaining = state.totalTicks;

        // shakemagnitude is pushed twice at 0x499FB8, once for each axis, so
        // the horizontal and vertical amplitudes are always the same number.
        state.magnitudeX += magnitude;
        state.magnitudeY += magnitude;

        if (state.totalTicks > 0)
        {
            state.running = true;
        }
    }

    std::pair<int, int> screenShakeAmplitudes(const ScreenShakeState& state)
    {
        if (!state.running || state.ticksRemaining <= 0 || state.totalTicks <= 0)
        {
            return {0, 0};
        }

        // Linear ramp down: full strength on the first frame, nothing on the
        // last. The divisor is the duration the shake started with, which is
        // why a second explosion arriving mid-shake restarts the ramp.
        return {
            state.magnitudeX * state.ticksRemaining / state.totalTicks,
            state.magnitudeY * state.ticksRemaining / state.totalTicks};
    }

    void advanceScreenShake(ScreenShakeState& state)
    {
        if (!state.running)
        {
            return;
        }

        if (state.ticksRemaining <= 0)
        {
            state.running = false;
            return;
        }

        --state.ticksRemaining;
    }

    const char* missionDisplayName(UnitActivity activity)
    {
        switch (activity)
        {
            case UnitActivity::Standby:
                return "Standby";
            case UnitActivity::Moving:
                return "Moving";
            case UnitActivity::Attacking:
                return "Attacking";
            case UnitActivity::Annihilating:
                return "Annihilating";
            case UnitActivity::Nanolathing:
                return "Nanolathing";
            case UnitActivity::Guarding:
                return "Guarding";
            case UnitActivity::Reclaiming:
                return "Reclaiming";
            case UnitActivity::Resurrecting:
                // 0x5013A0, the display name on row 42 of the ground table.
                return "Resurrecting";
            case UnitActivity::Repairing:
                return "Repairing";
            case UnitActivity::Patrolling:
                return "Patrolling";
            case UnitActivity::Capturing:
                return "Capturing";
            case UnitActivity::Loading:
                return "Loading";
            case UnitActivity::Unloading:
                return "Unloading";
            case UnitActivity::Landing:
                return "Landing";
            case UnitActivity::UnderRepair:
                return "Under repair";
            case UnitActivity::UnderConstruction:
                return "Under construction";
            case UnitActivity::BeingTransported:
                return "Being transported";
            case UnitActivity::Paralyzed:
                return "Paralyzed";
            case UnitActivity::SelfDestructing:
                return "SELF DESTRUCT ENGAGED";
        }

        return "Standby";
    }

    UnitActivity unitActivity(const UnitState& unit, bool underConstruction, bool weaponQueued)
    {
        // The states that displace an order come first, because in the
        // original they are missions in their own right and sit at the front
        // of the unit's mission list: GetBuilt (13), BeCarried (14),
        // Paralyze (12) and SelfDestruct (10).
        if (unit.selfDestructTime)
        {
            return UnitActivity::SelfDestructing;
        }

        if (underConstruction)
        {
            return UnitActivity::UnderConstruction;
        }

        if (unit.carriedBy)
        {
            return UnitActivity::BeingTransported;
        }

        if (unit.paralyzedUntil)
        {
            return UnitActivity::Paralyzed;
        }

        if (unit.orders.empty())
        {
            // A launcher with a round on order is running BuildWeapon (ground
            // mission 9), which shares Nanolathing with the two build
            // missions. RWE keeps the queue on the weapon rather than in the
            // order list, so it has to be asked about separately.
            if (weaponQueued)
            {
                return UnitActivity::Nanolathing;
            }

            return UnitActivity::Standby;
        }

        return match(
            unit.orders.front(),
            [](const MoveOrder&) { return UnitActivity::Moving; },
            [](const AttackOrder&) { return UnitActivity::Attacking; },
            [](const BuildOrder&) { return UnitActivity::Nanolathing; },
            [](const BuggerOffOrder&) { return UnitActivity::Moving; },
            [](const CompleteBuildOrder&) { return UnitActivity::Nanolathing; },
            [](const GuardOrder&) { return UnitActivity::Guarding; },
            [](const ReclaimOrder&) { return UnitActivity::Reclaiming; },
            [](const ResurrectOrder&) { return UnitActivity::Resurrecting; },
            [](const RepairOrder&) { return UnitActivity::Repairing; },
            [](const PatrolOrder&) { return UnitActivity::Patrolling; },
            [](const CaptureOrder&) { return UnitActivity::Capturing; },
            [](const LoadOrder&) { return UnitActivity::Loading; },
            [](const UnloadOrder&) { return UnitActivity::Unloading; },
            [](const DgunOrder&) { return UnitActivity::Annihilating; },
            [](const LandOnAirBaseOrder&) { return UnitActivity::Landing; });
    }

    std::optional<UnitId> unitOrderTargetUnit(const UnitState& unit)
    {
        if (unit.orders.empty())
        {
            return std::nullopt;
        }

        return match(
            unit.orders.front(),
            [](const MoveOrder&) { return std::optional<UnitId>(); },
            [](const AttackOrder& o) {
                if (auto target = std::get_if<UnitId>(&o.target); target != nullptr)
                {
                    return std::optional<UnitId>(*target);
                }
                return std::optional<UnitId>();
            },
            [&](const BuildOrder&) { return unit.buildOrderUnitId; },
            [](const BuggerOffOrder&) { return std::optional<UnitId>(); },
            [](const CompleteBuildOrder& o) { return std::optional<UnitId>(o.target); },
            [](const GuardOrder& o) { return std::optional<UnitId>(o.target); },
            [](const ReclaimOrder& o) {
                if (auto target = std::get_if<UnitId>(&o.target); target != nullptr)
                {
                    return std::optional<UnitId>(*target);
                }
                return std::optional<UnitId>();
            },
            // A resurrect order names a feature, never a unit.
            [](const ResurrectOrder&) { return std::optional<UnitId>(); },
            [](const RepairOrder& o) { return std::optional<UnitId>(o.target); },
            [](const PatrolOrder&) { return std::optional<UnitId>(); },
            [](const CaptureOrder& o) { return std::optional<UnitId>(o.target); },
            [](const LoadOrder& o) { return std::optional<UnitId>(o.target); },
            [](const UnloadOrder&) { return std::optional<UnitId>(); },
            [](const DgunOrder& o) {
                if (auto target = std::get_if<UnitId>(&o.target); target != nullptr)
                {
                    return std::optional<UnitId>(*target);
                }
                return std::optional<UnitId>();
            },
            [](const LandOnAirBaseOrder& o) { return std::optional<UnitId>(o.target); });
    }

    std::string killsCaption(unsigned int kills)
    {
        if (kills == 0)
        {
            return std::string();
        }

        auto noun = kills == 1 ? std::string("kill") : std::string("kills");
        auto caption = std::to_string(kills) + " " + noun;
        if (kills > 4)
        {
            caption += " - Veteran";
        }

        return caption;
    }

    std::optional<std::reference_wrapper<const MapFeature>> tryGetHoveredFeature(
        const GameSimulation& simulation, const std::optional<FeatureId>& hoveredFeature)
    {
        if (!hoveredFeature)
        {
            return std::nullopt;
        }

        return simulation.tryGetFeature(*hoveredFeature);
    }

    bool shouldStartNextMusicTrack(bool leavingScene, bool musicPlaying, GameTime gameTime, GameTime holdOffUntil)
    {
        return !leavingScene && !musicPlaying && gameTime >= holdOffUntil;
    }

    std::size_t nextMusicTrackIndex(MusicTrackMode mode, const std::vector<std::string>& tracks, const std::string& last, int step, unsigned int randomValue)
    {
        const auto count = tracks.size();
        if (mode == MusicTrackMode::Random)
        {
            return randomValue % count;
        }

        auto it = std::find(tracks.begin(), tracks.end(), last);
        if (it == tracks.end())
        {
            return 0;
        }
        auto index = static_cast<std::size_t>(it - tracks.begin());

        auto move = step;
        if (mode == MusicTrackMode::PlayAll && move == 0)
        {
            move = 1;
        }
        if (move > 0)
        {
            return (index + 1) % count;
        }
        if (move < 0)
        {
            return (index + count - 1) % count;
        }
        return index;
    }

    bool featureCanBeReclaimed(const GameSimulation& sim, FeatureId featureId)
    {
        auto feature = sim.tryGetFeature(featureId);
        if (!feature)
        {
            return false;
        }

        return sim.getFeatureDefinition(feature->get().featureName).reclaimable;
    }
}
