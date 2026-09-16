#pragma once

#include <rwe/render/GraphicsContext.h>
#include <unordered_map>

namespace rwe
{
    struct BasicColorShader
    {
        ShaderProgramHandle handle;
        UniformLocation mvpMatrix;
        UniformLocation alpha;
    };

    struct BasicTextureShader
    {
        ShaderProgramHandle handle;
        UniformLocation mvpMatrix;
        UniformLocation tint;
        UniformLocation desaturate;
    };

    /**
     * basicTexture, plus the building halo's coverage mask. The one caller
     * that wants a standing feature -- a tree -- to occlude a building for
     * the halo test uses this instead. See basicTextureMasked.frag.
     */
    struct BasicTextureMaskedShader
    {
        ShaderProgramHandle handle;
        UniformLocation mvpMatrix;
        UniformLocation tint;
        UniformLocation desaturate;
        /** Always 0.5, "anything else solid". See UnitTextureShader::maskValue. */
        UniformLocation maskValue;
    };

    struct MapTerrainShader
    {
        ShaderProgramHandle handle;
        UniformLocation mvpMatrix;
        UniformLocation fogSampler;
        UniformLocation fogEnabled;
        UniformLocation fogTransform;
    };

    struct UnitTextureShader
    {
        ShaderProgramHandle handle;
        UniformLocation mvpMatrix;
        UniformLocation modelMatrix;
        UniformLocation seaLevel;
        UniformLocation shadeStrength;
        UniformLocation alpha;
        UniformLocation paletteIndexSampler;
        UniformLocation shadeTableSampler;
        /** 1 for a cached piece of a finished building, 0.5 otherwise. */
        UniformLocation maskValue;
    };

    struct UnitShadowShader
    {
        ShaderProgramHandle handle;
        UniformLocation vpMatrix;
        UniformLocation modelMatrix;
        UniformLocation groundHeight;
        UniformLocation projected;
        UniformLocation shadowOriginY;
    };

    struct UnitBuildShader
    {
        ShaderProgramHandle handle;
        UniformLocation mvpMatrix;
        UniformLocation modelMatrix;
        UniformLocation unitY;
        UniformLocation seaLevel;
        UniformLocation shadeStrength;
        UniformLocation unitHeight;
        UniformLocation buildRatio;
        UniformLocation aboveMode;
        UniformLocation bandMode;
        UniformLocation belowMode;
        UniformLocation buildColorA;
        UniformLocation buildColorB;
        UniformLocation paletteIndexSampler;
        UniformLocation shadeTableSampler;
    };

    struct FlashEffectShader
    {
        ShaderProgramHandle handle;
        UniformLocation mvpMatrix;
        UniformLocation intensity;
        UniformLocation color;
    };

    struct WorldPostShader
    {
        ShaderProgramHandle handle;
        UniformLocation dodgeMask;
        UniformLocation gamma;
        UniformLocation buildingMask;
        UniformLocation alphaTable;
        UniformLocation haloStrength;
        UniformLocation haloSaturation;
        UniformLocation haloRedShift;
        UniformLocation selectiveAntiAlias;
        UniformLocation antiAliasUnits;
    };

    class ShaderService
    {
    public:
        static ShaderService createShaderService(GraphicsContext& graphics);

    private:
        static std::string slurpFile(const std::string& filename);

        static ShaderProgramHandle loadShader(GraphicsContext& graphics, const std::string& vertexShaderName, const std::string& fragmentShaderName, const std::vector<AttribMapping>& attribs);

    public:
        BasicColorShader basicColor;
        BasicTextureShader basicTexture;
        BasicTextureMaskedShader basicTextureMasked;
        MapTerrainShader mapTerrain;
        UnitTextureShader unitTexture;
        UnitShadowShader unitShadow;
        UnitBuildShader unitBuild;
        FlashEffectShader flashEffect;
        WorldPostShader worldPost;
    };
}
