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

    /**
     * A category of model drawn as palette indices, for the post pass to run
     * the original's anti-alias table over. See unitMask.frag.
     */
    struct UnitMaskShader
    {
        ShaderProgramHandle handle;
        UniformLocation mvpMatrix;
        UniformLocation paletteIndexSampler;
        /** 1 for a cached building piece, 0.5 for an occluder. See unitMask.frag. */
        UniformLocation maskAlpha;
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
        MapTerrainShader mapTerrain;
        UnitTextureShader unitTexture;
        UnitShadowShader unitShadow;
        UnitBuildShader unitBuild;
        FlashEffectShader flashEffect;
        UnitMaskShader unitMask;
        WorldPostShader worldPost;
    };
}
