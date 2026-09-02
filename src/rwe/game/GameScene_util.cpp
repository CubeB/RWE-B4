#include "GameScene_util.h"

#include <array>
#include <algorithm>
#include <rwe/util/Index.h>

namespace rwe
{
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
            if (!item.second.predecessor)
            {
                continue;
            }

            auto start = (*item.second.predecessor)->vertex;
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

    Matrix4f getPieceTransformForRender(const std::string& pieceName, const UnitModelDefinition& modelDefinition, const std::vector<UnitMesh>& pieces, float frac)
    {
        assert(modelDefinition.pieces.size() == pieces.size());

        std::optional<std::string> parentPiece = pieceName;
        auto matrix = Matrix4f::identity();

        do
        {
            auto pieceIndexIt = modelDefinition.pieceIndicesByName.find(toUpper(*parentPiece));
            if (pieceIndexIt == modelDefinition.pieceIndicesByName.end())
            {
                throw std::runtime_error("missing piece definition: " + *parentPiece);
            }
            const auto& pieceDef = modelDefinition.pieces[pieceIndexIt->second];

            parentPiece = pieceDef.parent;

            auto pieceStateIt = pieces.begin() + pieceIndexIt->second;

            auto position = lerp(simVectorToFloat(pieceDef.origin + pieceStateIt->previousOffset), simVectorToFloat(pieceDef.origin + pieceStateIt->offset), frac);
            auto rotationX = angleLerp(toRadians(pieceStateIt->previousRotationX).value, toRadians(pieceStateIt->rotationX).value, frac);
            auto rotationY = angleLerp(toRadians(pieceStateIt->previousRotationY).value, toRadians(pieceStateIt->rotationY).value, frac);
            auto rotationZ = angleLerp(toRadians(pieceStateIt->previousRotationZ).value, toRadians(pieceStateIt->rotationZ).value, frac);
            matrix = Matrix4f::translation(position) * Matrix4f::rotationZXY(Vector3f(rotationX, rotationY, rotationZ)) * matrix;
        } while (parentPiece);

        return matrix;
    }

    Matrix4f getPieceTransformForRender(const std::string& pieceName, const UnitModelDefinition& modelDefinition)
    {
        std::optional<std::string> parentPiece = pieceName;
        auto pos = SimVector(0_ss, 0_ss, 0_ss);

        do
        {
            auto pieceIndexIt = modelDefinition.pieceIndicesByName.find(toUpper(*parentPiece));
            if (pieceIndexIt == modelDefinition.pieceIndicesByName.end())
            {
                throw std::runtime_error("missing piece definition: " + *parentPiece);
            }

            const auto& pieceDef = modelDefinition.pieces[pieceIndexIt->second];

            parentPiece = pieceDef.parent;

            pos += pieceDef.origin;
        } while (parentPiece);

        return Matrix4f::translation(simVectorToFloat(pos));
    }

    void drawShaderMesh(
        const Matrix4f& viewProjectionMatrix,
        const ShaderMesh& mesh,
        const Matrix4f& matrix,
        bool shaded,
        PlayerColorIndex playerColorIndex,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        std::vector<UnitTextureMeshRenderInfo>& batch)
    {
        auto mvpMatrix = viewProjectionMatrix * matrix;

        if (mesh.vertices)
        {
            batch.push_back(UnitTextureMeshRenderInfo{&*mesh.vertices, matrix, mvpMatrix, shaded, unitTextureAtlas});
        }
        if (mesh.teamVertices)
        {
            batch.push_back(UnitTextureMeshRenderInfo{&*mesh.teamVertices, matrix, mvpMatrix, shaded, unitTeamTextureAtlases.at(playerColorIndex.value).get()});
        }
    }

    void drawShaderMeshShadow(
        const Matrix4f& viewProjectionMatrix,
        const ShaderMesh& mesh,
        const Matrix4f& matrix,
        float groundHeight,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        std::vector<UnitTextureShadowMeshRenderInfo>& batch)
    {
        if (mesh.vertices)
        {
            batch.push_back(UnitTextureShadowMeshRenderInfo{&*mesh.vertices, matrix, viewProjectionMatrix, unitTextureAtlas, groundHeight});
        }
        if (mesh.teamVertices)
        {
            batch.push_back(UnitTextureShadowMeshRenderInfo{&*mesh.teamVertices, matrix, viewProjectionMatrix, unitTeamTextureAtlases.at(0).get(), groundHeight});
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
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        UnitMeshBatch& batch)
    {
        assert(modelDefinition.pieces.size() == meshes.size());

        for (Index i = 0; i < getSize(modelDefinition.pieces); ++i)
        {
            const auto& pieceDef = modelDefinition.pieces[i];
            const auto& mesh = meshes[i];
            if (!mesh.visible)
            {
                continue;
            }

            auto matrix = modelMatrix * getPieceTransformForRender(pieceDef.name, modelDefinition, meshes, frac);

            const auto& resolvedMesh = *gameMediaDatabase.getUnitPieceMesh(objectName, pieceDef.name).value().get().mesh;
            drawShaderMesh(viewProjectionMatrix, resolvedMesh, matrix, mesh.shaded, playerColorIndex, unitTextureAtlas, unitTeamTextureAtlases, batch.meshes);
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
        float groundHeight,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        UnitShadowMeshBatch& batch)
    {
        assert(modelDefinition.pieces.size() == meshes.size());

        for (Index i = 0; i < getSize(modelDefinition.pieces); ++i)
        {
            const auto& pieceDef = modelDefinition.pieces[i];
            const auto& mesh = meshes[i];
            if (!mesh.visible)
            {
                continue;
            }

            auto matrix = modelMatrix * getPieceTransformForRender(pieceDef.name, modelDefinition, meshes, frac);

            const auto& resolvedMesh = *gameMediaDatabase.getUnitPieceMesh(objectName, pieceDef.name).value().get().mesh;
            drawShaderMeshShadow(viewProjectionMatrix, resolvedMesh, matrix, groundHeight, unitTextureAtlas, unitTeamTextureAtlases, batch.meshes);
        }
    }

    void drawUnitShadowMeshNoPieces(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const std::string& objectName,
        const UnitModelDefinition& modelDefinition,
        const Matrix4f& modelMatrix,
        float groundHeight,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        UnitShadowMeshBatch& batch)
    {
        for (Index i = 0; i < getSize(modelDefinition.pieces); ++i)
        {
            const auto& pieceDef = modelDefinition.pieces[i];

            auto matrix = modelMatrix * getPieceTransformForRender(pieceDef.name, modelDefinition);

            const auto& resolvedMesh = *gameMediaDatabase.getUnitPieceMesh(objectName, pieceDef.name).value().get().mesh;
            drawShaderMeshShadow(viewProjectionMatrix, resolvedMesh, matrix, groundHeight, unitTextureAtlas, unitTeamTextureAtlases, batch.meshes);
        }
    }

    void drawBuildingShaderMesh(
        const Matrix4f& viewProjectionMatrix,
        const ShaderMesh& mesh,
        const Matrix4f& matrix,
        bool shaded,
        const BuildPhase& buildPhase,
        float unitY,
        float unitHeight,
        PlayerColorIndex playerColorIndex,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        std::vector<UnitBuildingMeshRenderInfo>& batch)
    {
        auto mvpMatrix = viewProjectionMatrix * matrix;
        if (mesh.vertices)
        {
            batch.push_back(UnitBuildingMeshRenderInfo{&*mesh.vertices, matrix, mvpMatrix, shaded, unitTextureAtlas, unitY, unitHeight, buildPhase.ratio, buildPhase.aboveMode, buildPhase.bandMode, buildPhase.belowMode, buildPhase.colorA, buildPhase.colorB});
        }
        if (mesh.teamVertices)
        {
            batch.push_back(UnitBuildingMeshRenderInfo{&*mesh.teamVertices, matrix, mvpMatrix, shaded, unitTeamTextureAtlases.at(playerColorIndex.value).get(), unitY, unitHeight, buildPhase.ratio, buildPhase.aboveMode, buildPhase.bandMode, buildPhase.belowMode, buildPhase.colorA, buildPhase.colorB});
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
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        UnitMeshBatch& batch)
    {
        for (Index i = 0; i < getSize(modelDefinition.pieces); ++i)
        {
            const auto& pieceDef = modelDefinition.pieces[i];
            const auto& mesh = meshes[i];
            if (!mesh.visible)
            {
                continue;
            }

            auto matrix = modelMatrix * getPieceTransformForRender(pieceDef.name, modelDefinition, meshes, frac);

            const auto& resolvedMesh = *gameMediaDatabase.getUnitPieceMesh(objectName, pieceDef.name).value().get().mesh;
            drawBuildingShaderMesh(viewProjectionMatrix, resolvedMesh, matrix, mesh.shaded, buildPhase, unitY, simScalarToFloat(modelDefinition.height), playerColorIndex, unitTextureAtlas, unitTeamTextureAtlases, batch.buildingMeshes);
        }
    }

    void drawProjectileUnitMesh(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const std::string& objectName,
        const UnitModelDefinition& modelDefinition,
        const Matrix4f& modelMatrix,
        PlayerColorIndex playerColorIndex,
        bool shaded,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        UnitMeshBatch& batch)
    {
        for (const auto& pieceDef : modelDefinition.pieces)
        {
            auto matrix = modelMatrix * getPieceTransformForRender(pieceDef.name, modelDefinition);
            const auto& resolvedMesh = *gameMediaDatabase.getUnitPieceMesh(objectName, pieceDef.name).value().get().mesh;
            drawShaderMesh(viewProjectionMatrix, resolvedMesh, matrix, shaded, playerColorIndex, unitTextureAtlas, unitTeamTextureAtlases, batch.meshes);
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
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        UnitMeshBatch& batch)
    {
        auto position = lerp(simVectorToFloat(unit.previousPosition), simVectorToFloat(unit.position), frac);
        auto rotation = angleLerp(toRadians(unit.previousRotation).value, toRadians(unit.rotation).value, frac);
        auto transform = unitRenderTransform(unit, unitDefinition, position, rotation, frac);
        if (unit.isBeingBuilt(unitDefinition))
        {
            auto buildPhase = computeBuildPhase(unit.getPreciseCompletePercent(unitDefinition), unitIndex, gameTime);
            drawBuildingUnitMesh(gameMediaDatabase, viewProjectionMatrix, unitDefinition.objectName, modelDefinition, unit.pieces, transform, buildPhase, position.y, playerColorIndex, frac, unitTextureAtlas, unitTeamTextureAtlases, batch);
        }
        else
        {
            drawUnitMesh(gameMediaDatabase, viewProjectionMatrix, unitDefinition.objectName, modelDefinition, unit.pieces, transform, playerColorIndex, frac, unitTextureAtlas, unitTeamTextureAtlases, batch);
        }
    }

    void drawMeshFeature(
        const std::unordered_map<std::string, UnitModelDefinition>& modelDefinitions,
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const MapFeature& feature,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        UnitMeshBatch& batch)
    {
        const auto& featureMediaInfo = gameMediaDatabase.getFeature(feature.featureName);

        if (auto objectInfo = std::get_if<FeatureObjectInfo>(&featureMediaInfo.renderInfo); objectInfo != nullptr)
        {
            const auto& modelDefinition = modelDefinitions.at(objectInfo->objectName);
            auto matrix = Matrix4f::translation(simVectorToFloat(feature.position)) * Matrix4f::rotationY(toRadians(feature.rotation).value);
            drawProjectileUnitMesh(gameMediaDatabase, viewProjectionMatrix, objectInfo->objectName, modelDefinition, matrix, PlayerColorIndex(0), true, unitTextureAtlas, unitTeamTextureAtlases, batch);
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
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        UnitShadowMeshBatch& batch)
    {
        auto position = lerp(simVectorToFloat(unit.previousPosition), simVectorToFloat(unit.position), frac);
        auto rotation = angleLerp(toRadians(unit.previousRotation).value, toRadians(unit.rotation).value, frac);
        auto transform = unitRenderTransform(unit, unitDefinition, position, rotation, frac);

        drawUnitShadowMesh(gameMediaDatabase, viewProjectionMatrix, unitDefinition.objectName, modelDefinition, unit.pieces, transform, frac, groundHeight, unitTextureAtlas, unitTeamTextureAtlases, batch);
    }

    void drawFeatureMeshShadow(
        const std::unordered_map<std::string, UnitModelDefinition>& modelDefinitions,
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const MapFeature& feature,
        float groundHeight,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
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

        drawUnitShadowMeshNoPieces(gameMediaDatabase, viewProjectionMatrix, objectInfo->objectName, modelDefinition, matrix, groundHeight, unitTextureAtlas, unitTeamTextureAtlases, batch);
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
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        UnitMeshBatch& batch)
    {
        auto pieceMesh = gameMediaDatabase.getUnitPieceMesh(objectName, pieceName);
        if (!pieceMesh)
        {
            return;
        }
        drawShaderMesh(viewProjectionMatrix, *pieceMesh->get().mesh, matrix, true, playerColorIndex, unitTextureAtlas, unitTeamTextureAtlases, batch.meshes);
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

    void drawUnitWireframe(
        const GameMediaDatabase& gameMediaDatabase,
        const UnitState& unit,
        const UnitDefinition& unitDefinition,
        const UnitModelDefinition& modelDefinition,
        float frac,
        const Vector3f& toCamera,
        const Vector3f& color,
        ColoredMeshBatch& batch)
    {
        assert(modelDefinition.pieces.size() == unit.pieces.size());

        auto position = lerp(simVectorToFloat(unit.previousPosition), simVectorToFloat(unit.position), frac);
        auto rotation = angleLerp(toRadians(unit.previousRotation).value, toRadians(unit.rotation).value, frac);
        auto transform = unitRenderTransform(unit, unitDefinition, position, rotation, frac);

        // Edges resting on the ground trace the footprint; TA leaves those out.
        auto groundLevel = position.y + 1.0f;

        // Lift the lines slightly towards the camera so they pass the depth
        // test against the surface they outline, while anything the model
        // itself hides stays hidden.
        auto bias = toCamera * 0.75f;

        for (Index i = 0; i < getSize(modelDefinition.pieces); ++i)
        {
            const auto& pieceDef = modelDefinition.pieces[i];
            if (!unit.pieces[i].visible)
            {
                continue;
            }

            auto matrix = transform * getPieceTransformForRender(pieceDef.name, modelDefinition, unit.pieces, frac);
            const auto& pieceInfo = gameMediaDatabase.getUnitPieceMesh(unitDefinition.objectName, pieceDef.name).value().get();
            if (!pieceInfo.edges)
            {
                continue;
            }

            auto origin = matrix * Vector3f(0.0f, 0.0f, 0.0f);
            auto facesCamera = [&](const Vector3f& normal) {
                return ((matrix * normal) - origin).dot(toCamera) > 0.0f;
            };

            auto facesUp = [&](const Vector3f& normal) {
                return ((matrix * normal) - origin).y > 0.5f;
            };

            for (const auto& edge : *pieceInfo.edges)
            {
                if (!facesCamera(edge.normalA) && !(edge.normalB && facesCamera(*edge.normalB)))
                {
                    continue;
                }
                auto a = matrix * edge.start;
                auto b = matrix * edge.end;
                if (a.y <= groundLevel && b.y <= groundLevel)
                {
                    // A ground-level edge is skipped when it is just where a
                    // wall meets the ground, but kept when it outlines a
                    // floor polygon such as an aircraft plant's landing pad.
                    bool outlinesFloor = facesUp(edge.normalA) || (edge.normalB && facesUp(*edge.normalB));
                    if (!outlinesFloor)
                    {
                        continue;
                    }
                }
                pushLine(batch.lines, a + bias, b + bias, color);
            }
        }
    }

    void drawUnitSilhouette(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const UnitState& unit,
        const UnitDefinition& unitDefinition,
        const UnitModelDefinition& modelDefinition,
        float frac,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        std::vector<UnitTextureMeshRenderInfo>& out)
    {
        auto position = lerp(simVectorToFloat(unit.previousPosition), simVectorToFloat(unit.position), frac);
        auto rotation = angleLerp(toRadians(unit.previousRotation).value, toRadians(unit.rotation).value, frac);
        auto transform = unitRenderTransform(unit, unitDefinition, position, rotation, frac);

        UnitMeshBatch batch;
        drawUnitMesh(gameMediaDatabase, viewProjectionMatrix, unitDefinition.objectName, modelDefinition, unit.pieces, transform, PlayerColorIndex(0), frac, unitTextureAtlas, unitTeamTextureAtlases, batch);
        out.insert(out.end(), batch.meshes.begin(), batch.meshes.end());
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
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const VectorMap<Projectile, ProjectileIdTag>& projectiles,
        GameTime currentTime,
        float frac,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        ColoredMeshBatch& coloredMeshbatch,
        SpriteBatch& spriteBatch,
        UnitMeshBatch& unitMeshBatch)
    {
        for (const auto& e : projectiles)
        {
            const auto& projectile = e.second;
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
                    // Bombs dropped by hovering aircraft can spawn with
                    // zero velocity for a tick before gravity kicks in;
                    // fall back to a sane forward direction instead of
                    // crashing on normalize().
                    auto transform = Matrix4f::translation(position)
                        * pointDirection(simVectorToFloat(projectile.velocity).normalizedOr(Vector3f(0.0f, 0.0f, 1.0f)))
                        * rotationModeToMatrix(m.rotationMode);
                    const auto& modelDefinition = sim.unitModelDefinitions.at(m.objectName);
                    drawProjectileUnitMesh(gameMediaDatabase, viewProjectionMatrix, m.objectName, modelDefinition, transform, PlayerColorIndex(0), false, unitTextureAtlas, unitTeamTextureAtlases, unitMeshBatch);
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

    void drawSelectionRect(const GameMediaDatabase& gameMediaDatabase, const Matrix4f& viewProjectionMatrix, const UnitState& unit, const UnitDefinition& unitDefinition, float frac, ColoredMeshesBatch& batch)
    {
        auto selectionMesh = gameMediaDatabase.getSelectionMesh(unitDefinition.objectName);

        auto position = lerp(simVectorToFloat(unit.previousPosition), simVectorToFloat(unit.position), frac);

        // try to ensure that the selection rectangle vertices
        // are aligned with the middle of pixels,
        // to prevent discontinuities in the drawn lines.
        Vector3f snappedPosition(
            snapToInterval(position.x, 1.0f) + 0.5f,
            snapToInterval(position.y, 2.0f),
            snapToInterval(position.z, 1.0f) + 0.5f);

        auto rotation = angleLerp(toRadians(unit.previousRotation).value, toRadians(unit.rotation).value, frac);
        auto matrix = Matrix4f::translation(snappedPosition) * Matrix4f::rotationY(rotation);
        auto mvpMatrix = viewProjectionMatrix * matrix;

        batch.meshes.push_back(ColoredMeshRenderInfo{selectionMesh.value().get(), mvpMatrix});
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
}
