#include "ShaderService.h"
#include <fstream>
#include <sstream>

namespace rwe
{
    ShaderService ShaderService::createShaderService(GraphicsContext& graphics)
    {
        ShaderService s;

        std::vector<AttribMapping> texturedVertexAttribs{
            AttribMapping{"position", 0},
            AttribMapping{"texCoord", 1}};

        std::vector<AttribMapping> coloredVertexAttribs{
            AttribMapping{"position", 0},
            AttribMapping{"color", 1}};

        // The unit meshes interleave position, texCoord, normal and enable
        // arrays 0/1/2 in that order. A program that reads the normal must
        // bind it to 2 explicitly: left to the linker it lands anywhere,
        // and on some drivers the lighting silently reads garbage.
        std::vector<AttribMapping> texturedNormalVertexAttribs{
            AttribMapping{"position", 0},
            AttribMapping{"texCoord", 1},
            AttribMapping{"normal", 2}};

        s.basicColor.handle = loadShader(graphics, "shaders/basicColor.vert", "shaders/basicColor.frag", coloredVertexAttribs);
        s.basicColor.mvpMatrix = graphics.getUniformLocation(s.basicColor.handle.get(), "mvpMatrix");
        s.basicColor.alpha = graphics.getUniformLocation(s.basicColor.handle.get(), "alpha");

        s.basicTexture.handle = loadShader(graphics, "shaders/basicTexture.vert", "shaders/basicTexture.frag", texturedVertexAttribs);
        s.basicTexture.mvpMatrix = graphics.getUniformLocation(s.basicTexture.handle.get(), "mvpMatrix");
        s.basicTexture.tint = graphics.getUniformLocation(s.basicTexture.handle.get(), "tint");
        s.basicTexture.desaturate = graphics.getUniformLocation(s.basicTexture.handle.get(), "desaturate");

        s.mapTerrain.handle = loadShader(graphics, "shaders/mapTerrain.vert", "shaders/mapTerrain.frag", texturedVertexAttribs);
        s.mapTerrain.mvpMatrix = graphics.getUniformLocation(s.mapTerrain.handle.get(), "mvpMatrix");
        s.mapTerrain.fogSampler = graphics.getUniformLocation(s.mapTerrain.handle.get(), "fogSampler");
        s.mapTerrain.fogEnabled = graphics.getUniformLocation(s.mapTerrain.handle.get(), "fogEnabled");
        s.mapTerrain.fogTransform = graphics.getUniformLocation(s.mapTerrain.handle.get(), "fogTransform");

        s.unitTexture.handle = loadShader(graphics, "shaders/unitTexture.vert", "shaders/unitTexture.frag", texturedNormalVertexAttribs);
        s.unitTexture.mvpMatrix = graphics.getUniformLocation(s.unitTexture.handle.get(), "mvpMatrix");
        s.unitTexture.modelMatrix = graphics.getUniformLocation(s.unitTexture.handle.get(), "modelMatrix");
        s.unitTexture.seaLevel = graphics.getUniformLocation(s.unitTexture.handle.get(), "seaLevel");
        s.unitTexture.shadeStrength = graphics.getUniformLocation(s.unitTexture.handle.get(), "shadeStrength");
        s.unitTexture.alpha = graphics.getUniformLocation(s.unitTexture.handle.get(), "alpha");
        s.unitTexture.paletteIndexSampler = graphics.getUniformLocation(s.unitTexture.handle.get(), "paletteIndexSampler");
        s.unitTexture.shadeTableSampler = graphics.getUniformLocation(s.unitTexture.handle.get(), "shadeTableSampler");

        s.unitShadow.handle = loadShader(graphics, "shaders/unitShadow.vert", "shaders/unitShadow.frag", texturedVertexAttribs);
        s.unitShadow.vpMatrix = graphics.getUniformLocation(s.unitShadow.handle.get(), "vpMatrix");
        s.unitShadow.modelMatrix = graphics.getUniformLocation(s.unitShadow.handle.get(), "modelMatrix");
        s.unitShadow.groundHeight = graphics.getUniformLocation(s.unitShadow.handle.get(), "groundHeight");
        s.unitShadow.projected = graphics.getUniformLocation(s.unitShadow.handle.get(), "projected");
        s.unitShadow.shadowOriginY = graphics.getUniformLocation(s.unitShadow.handle.get(), "shadowOriginY");

        s.unitBuild.handle = loadShader(graphics, "shaders/unitBuild.vert", "shaders/unitBuild.frag", texturedNormalVertexAttribs);
        s.unitBuild.mvpMatrix = graphics.getUniformLocation(s.unitBuild.handle.get(), "mvpMatrix");
        s.unitBuild.unitY = graphics.getUniformLocation(s.unitBuild.handle.get(), "unitY");
        s.unitBuild.modelMatrix = graphics.getUniformLocation(s.unitBuild.handle.get(), "modelMatrix");
        s.unitBuild.seaLevel = graphics.getUniformLocation(s.unitBuild.handle.get(), "seaLevel");
        s.unitBuild.shadeStrength = graphics.getUniformLocation(s.unitBuild.handle.get(), "shadeStrength");
        s.unitBuild.unitHeight = graphics.getUniformLocation(s.unitBuild.handle.get(), "unitHeight");
        s.unitBuild.buildRatio = graphics.getUniformLocation(s.unitBuild.handle.get(), "buildRatio");
        s.unitBuild.aboveMode = graphics.getUniformLocation(s.unitBuild.handle.get(), "aboveMode");
        s.unitBuild.bandMode = graphics.getUniformLocation(s.unitBuild.handle.get(), "bandMode");
        s.unitBuild.belowMode = graphics.getUniformLocation(s.unitBuild.handle.get(), "belowMode");
        s.unitBuild.buildColorA = graphics.getUniformLocation(s.unitBuild.handle.get(), "buildColorA");
        s.unitBuild.buildColorB = graphics.getUniformLocation(s.unitBuild.handle.get(), "buildColorB");
        s.unitBuild.paletteIndexSampler = graphics.getUniformLocation(s.unitBuild.handle.get(), "paletteIndexSampler");
        s.unitBuild.shadeTableSampler = graphics.getUniformLocation(s.unitBuild.handle.get(), "shadeTableSampler");

        s.flashEffect.handle = loadShader(graphics, "shaders/flashEffect.vert", "shaders/flashEffect.frag", texturedVertexAttribs);
        s.flashEffect.mvpMatrix = graphics.getUniformLocation(s.flashEffect.handle.get(), "mvpMatrix");
        s.flashEffect.intensity = graphics.getUniformLocation(s.flashEffect.handle.get(), "intensity");
        s.flashEffect.color = graphics.getUniformLocation(s.flashEffect.handle.get(), "color");

        s.worldPost.handle = loadShader(graphics, "shaders/worldPost.vert", "shaders/worldPost.frag", texturedVertexAttribs);
        s.worldPost.dodgeMask = graphics.getUniformLocation(s.worldPost.handle.get(), "dodgeMask");
        s.worldPost.gamma = graphics.getUniformLocation(s.worldPost.handle.get(), "gamma");
        s.worldPost.buildingMask = graphics.getUniformLocation(s.worldPost.handle.get(), "buildingMask");
        s.worldPost.alphaTable = graphics.getUniformLocation(s.worldPost.handle.get(), "alphaTable");
        s.worldPost.haloStrength = graphics.getUniformLocation(s.worldPost.handle.get(), "haloStrength");
        s.worldPost.haloSaturation = graphics.getUniformLocation(s.worldPost.handle.get(), "haloSaturation");
        s.worldPost.haloRedShift = graphics.getUniformLocation(s.worldPost.handle.get(), "haloRedShift");

        s.unitMask.handle = loadShader(graphics, "shaders/unitMask.vert", "shaders/unitMask.frag", texturedVertexAttribs);
        s.unitMask.mvpMatrix = graphics.getUniformLocation(s.unitMask.handle.get(), "mvpMatrix");
        s.unitMask.paletteIndexSampler = graphics.getUniformLocation(s.unitMask.handle.get(), "paletteIndexSampler");

        return s;
    }

    std::string ShaderService::slurpFile(const std::string& filename)
    {
        std::ifstream inFile(filename, std::ios::binary);
        if (inFile.fail())
        {
            throw std::runtime_error("Failed to open file: " + filename);
        }

        std::stringstream strStream;
        strStream << inFile.rdbuf();
        return strStream.str();
    }

    ShaderProgramHandle ShaderService::loadShader(
        GraphicsContext& graphics,
        const std::string& vertexShaderName,
        const std::string& fragmentShaderName,
        const std::vector<AttribMapping>& attribs)
    {
        auto vertexShaderSource = slurpFile(vertexShaderName);
        auto vertexShader = graphics.compileVertexShader(vertexShaderSource);

        auto fragmentShaderSource = slurpFile(fragmentShaderName);
        auto fragmentShader = graphics.compileFragmentShader(fragmentShaderSource);

        return graphics.linkShaderProgram(vertexShader.get(), fragmentShader.get(), attribs);
    }
}
