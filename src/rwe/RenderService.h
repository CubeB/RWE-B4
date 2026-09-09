#pragma once

#include <rwe/ShaderService.h>
#include <rwe/game/FlashEffect.h>
#include <rwe/game/MapTerrainGraphics.h>
#include <rwe/render/GraphicsContext.h>
#include <rwe/sim/GameTime.h>
#include <vector>

namespace rwe
{
    struct ColoredMeshBatch
    {
        std::vector<GlColoredVertex> lines;
        std::vector<GlColoredVertex> triangles;
    };

    struct ColoredMeshRenderInfo
    {
        const GlMesh* mesh;
        Matrix4f mvpMatrix;
    };

    struct ColoredMeshesBatch
    {
        std::vector<ColoredMeshRenderInfo> meshes;
    };

    struct UnitTextureMeshRenderInfo
    {
        const GlMesh* mesh;
        Matrix4f modelMatrix;
        Matrix4f mvpMatrix;
        /**
         * How much of the shade table to apply: 0 leaves the texture as
         * authored, 1 is the measured PALETTE.SHD ramp. It already has the
         * piece's own COB shade flag and the VISUALS switch folded in, so the
         * renderer only has to hand it to the shader.
         */
        float shadeStrength;
        TextureIdentifier texture;
        /** The same atlas again, one byte a texel: that texel's palette index. */
        TextureIdentifier paletteIndexTexture;
        /**
         * What this mesh writes into the building halo's coverage mask: 1 for
         * a cached piece of a finished building, which is the only thing that
         * can carry a halo, and 0.5 for anything else solid. See
         * unitTexture.frag and TOTALA-EXE.md S:101.
         */
        float maskValue;
    };

    /**
     * One phase of the original's construction display. The model is split by
     * a threshold on height: above the line, in a four-unit band on it, and
     * below it, each drawn one of four ways.
     */
    enum class BuildFillMode
    {
        /** Not drawn at all. */
        Erase = 0,
        /** The slower of the two animated build colours. */
        ColorA = 1,
        /** The faster one. */
        ColorB = 2,
        /** The finished texture. */
        Texture = 3,
    };

    struct UnitBuildingMeshRenderInfo
    {
        const GlMesh* mesh;
        Matrix4f modelMatrix;
        Matrix4f mvpMatrix;
        /**
         * How much of the shade table to apply: 0 leaves the texture as
         * authored, 1 is the measured PALETTE.SHD ramp. It already has the
         * piece's own COB shade flag and the VISUALS switch folded in, so the
         * renderer only has to hand it to the shader.
         */
        float shadeStrength;
        TextureIdentifier texture;
        /** The same atlas again, one byte a texel: that texel's palette index. */
        TextureIdentifier paletteIndexTexture;
        float unitY;
        /** Height of the whole model, so the build fill can sweep bottom to top. */
        float unitHeight;
        /** Height of the line, in the original's units above the model's base. */
        float buildRatio;
        BuildFillMode aboveMode;
        BuildFillMode bandMode;
        BuildFillMode belowMode;
        Vector3f buildColorA;
        Vector3f buildColorB;
    };

    /**
     * The share of its own colour a cloaked unit keeps in each pixel it
     * covers. The original has no alpha at all: it composites the finished
     * unit into a private bitmap and lays that down through a 256x256
     * source-by-destination lookup table (`0x4B8500` -> `0x4CBF2C`), and that
     * table is built at `0x4BA772` as the nearest palette entry to the exact
     * midpoint of the two colours. So the ratio really is one half, not a
     * value someone picked because it looked right.
     */
    constexpr float CloakBlendFactor = 0.5f;

    struct UnitTextureShadowMeshRenderInfo
    {
        const GlMesh* mesh;
        Matrix4f modelMatrix;
        Matrix4f vpMatrix;
        TextureIdentifier texture;
        float groundHeight;
        /**
         * True for the projected shadow a building casts, false for the offset
         * copy of its own silhouette a unit casts. See TOTALA-EXE.md S:100.
         */
        bool projected;
        /** The one height an offset shadow takes its displacement from; unused when projected. */
        float shadowOriginY;
    };

    struct UnitMeshBatch
    {
        std::vector<UnitTextureMeshRenderInfo> meshes;
        std::vector<UnitBuildingMeshRenderInfo> buildingMeshes;

        /**
         * Cloaked units, held back so they go down over the solid ones and
         * blend with what is behind them rather than replacing it.
         */
        std::vector<UnitTextureMeshRenderInfo> cloakedMeshes;
    };

    struct UnitShadowMeshBatch
    {
        std::vector<UnitTextureShadowMeshRenderInfo> meshes;

        /** Models drawn as the camera sees them, whose silhouettes are cut out of the shadow. */
        std::vector<UnitTextureMeshRenderInfo> cutouts;
    };

    struct SpriteRenderInfo
    {
        const Sprite* sprite;
        Matrix4f mvpMatrix;
        bool translucent;
        /** Drawn in the grey of remembered-but-unseen ground. */
        bool fogged{false};
    };

    /**
     * Fog of war map laid over the terrain: a single-channel image of TA's fog
     * tiles at one texel per world unit, 0 = seen, mid = explored, 1 = unknown.
     */
    struct FogOverlay
    {
        TextureIdentifier texture;
        /** World x/z of the map's top-left corner. */
        float originX;
        float originZ;
        /** World width/height the map covers. */
        float width;
        float height;
    };

    struct SpriteBatch
    {
        std::vector<SpriteRenderInfo> sprites;
    };

    class RenderService
    {
    private:
        GraphicsContext* graphics;
        ShaderService* shaders;

        const Matrix4f* const viewProjectionMatrix;

    public:
        RenderService(
            GraphicsContext* graphics,
            ShaderService* shaders,
            const Matrix4f* viewProjectionMatrix);

        void drawMapTerrain(const MapTerrainGraphics& terrain, const Vector3f& cameraPosition, float viewportWidth, float viewportHeight, const std::optional<FogOverlay>& fog = std::nullopt);

        void drawMapTerrain(const MapTerrainGraphics& terrain, unsigned int x, unsigned int y, unsigned int width, unsigned int height, const std::optional<FogOverlay>& fog = std::nullopt);

        void fillScreen(float r, float g, float b, float a);

        void drawFlashes(GameTime currentTime, const std::vector<FlashEffect>& flashes);

        void drawBatch(const ColoredMeshBatch& batch, const Matrix4f& vpMatrix, float alpha = 1.0f);

        void drawUnitMeshBatch(const UnitMeshBatch& batch, float seaLevel, TextureIdentifier shadeTableTexture);

        void drawUnitShadowMeshBatch(const UnitShadowMeshBatch& batch);

        void drawSpriteBatch(const SpriteBatch& batch);

        void drawLineLoopsBatch(const ColoredMeshesBatch& batch);
    };
}
