#include "ShaderService.h"
#include <SDL3/SDL_filesystem.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace rwe
{
    namespace
    {
        /**
         * Where a shader actually lives.
         *
         * The names below are relative, so they resolved against the working
         * directory and nothing else. That works when the game is launched
         * from the build directory or the repo root, and it fails everywhere
         * else -- the AppImage puts the binary in usr/bin and the shaders in
         * usr/share/rwe, and a player starts it from wherever they downloaded
         * it, so the first shader was not found and the engine died after
         * "Initializing services" with no menu.
         *
         * Tried in order: the working directory, so anything that worked
         * before still works unchanged; beside the executable, which is where
         * CMake copies them for a development build; and the layout the
         * AppImage installs. If none of them has it, the original name is
         * returned so that the error still names what was asked for rather
         * than whichever guess happened to be last.
         */
        std::string resolveShaderPath(const std::string& name)
        {
            std::error_code ec;
            if (std::filesystem::exists(name, ec))
            {
                return name;
            }

            const char* base = SDL_GetBasePath();
            if (base == nullptr)
            {
                return name;
            }

            std::filesystem::path baseDir(base);
            for (const auto& candidate : {baseDir / name, baseDir / ".." / "share" / "rwe" / name})
            {
                if (std::filesystem::exists(candidate, ec))
                {
                    return candidate.string();
                }
            }

            return name;
        }
    }
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

        s.basicTextureMasked.handle = loadShader(graphics, "shaders/basicTexture.vert", "shaders/basicTextureMasked.frag", texturedVertexAttribs);
        s.basicTextureMasked.mvpMatrix = graphics.getUniformLocation(s.basicTextureMasked.handle.get(), "mvpMatrix");
        s.basicTextureMasked.tint = graphics.getUniformLocation(s.basicTextureMasked.handle.get(), "tint");
        s.basicTextureMasked.desaturate = graphics.getUniformLocation(s.basicTextureMasked.handle.get(), "desaturate");
        s.basicTextureMasked.maskValue = graphics.getUniformLocation(s.basicTextureMasked.handle.get(), "maskValue");

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
        s.unitTexture.maskValue = graphics.getUniformLocation(s.unitTexture.handle.get(), "maskValue");

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
        s.worldPost.selectiveAntiAlias = graphics.getUniformLocation(s.worldPost.handle.get(), "selectiveAntiAlias");
        s.worldPost.antiAliasUnits = graphics.getUniformLocation(s.worldPost.handle.get(), "antiAliasUnits");


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
        auto vertexShaderSource = slurpFile(resolveShaderPath(vertexShaderName));
        auto vertexShader = graphics.compileVertexShader(vertexShaderSource);

        auto fragmentShaderSource = slurpFile(resolveShaderPath(fragmentShaderName));
        auto fragmentShader = graphics.compileFragmentShader(fragmentShaderSource);

        return graphics.linkShaderProgram(vertexShader.get(), fragmentShader.get(), attribs);
    }
}
