#include "RenderService.h"

namespace rwe
{
    RenderService::RenderService(
        GraphicsContext* graphics,
        ShaderService* shaders,
        const Matrix4f* viewProjectionMatrix)
        : graphics(graphics),
          shaders(shaders),
          viewProjectionMatrix(viewProjectionMatrix)
    {
    }

    void RenderService::drawMapTerrain(const MapTerrainGraphics& terrain, unsigned int x, unsigned int y, unsigned int width, unsigned int height, const std::optional<FogOverlay>& fog)
    {
        std::unordered_map<TextureArrayIdentifier, std::vector<std::pair<unsigned int, unsigned int>>> batches;

        for (unsigned int dy = 0; dy < height; ++dy)
        {
            for (unsigned int dx = 0; dx < width; ++dx)
            {
                auto tileIndex = terrain.getTiles().get(x + dx, y + dy);
                const auto& tileTexture = terrain.getTileTexture(tileIndex);

                batches[tileTexture.texture.get()].emplace_back(dx, dy);
            }
        }

        const auto& shader = shaders->mapTerrain;
        graphics->bindShader(shader.handle.get());
        graphics->setUniformMatrix(shader.mvpMatrix, *viewProjectionMatrix);

        // The fog sampler is pointed at slot 1 whether or not there is fog to
        // draw. A sampler uniform that is never set reads zero, which would
        // leave this sampler2D on the same texture image unit as the tile
        // array's sampler2DArray; a program with two samplers of different
        // types on one unit fails GL's draw-time validation, and every
        // terrain draw is then dropped without a word. That is what turned
        // the ground solid black whenever there was no fog texture to bind,
        // while units, features and the minimap -- drawn by other programs --
        // came out perfectly well.
        graphics->setUniformInt(shader.fogSampler, 1);

        graphics->setUniformBool(shader.fogEnabled, fog.has_value());
        if (fog)
        {
            graphics->setUniformVec4(shader.fogTransform, fog->originX, fog->originZ, 1.0f / fog->width, 1.0f / fog->height);
            graphics->setActiveTextureSlot1();
            graphics->bindTexture(fog->texture);
        }

        // The tile array goes to slot 0, which its sampler reads by default.
        graphics->setActiveTextureSlot0();

        for (const auto& batch : batches)
        {
            std::vector<GlTextureArrayVertex> vertices;

            for (const auto& p : batch.second)
            {
                auto dx = p.first;
                auto dy = p.second;

                auto tileIndex = terrain.getTiles().get(x + dx, y + dy);
                auto tilePosition = simVectorToFloat(terrain.tileCoordinateToWorldCorner(x + dx, y + dy));

                const auto& tileTextureArray = terrain.getTileTexture(tileIndex);
                auto layerIndex = static_cast<float>(tileTextureArray.index);

                vertices.emplace_back(Vector3f(tilePosition.x, 0.0f, tilePosition.z), Vector3f(0.0f, 0.0f, layerIndex));
                vertices.emplace_back(Vector3f(tilePosition.x, 0.0f, tilePosition.z + simScalarToFloat(MapTerrainGraphics::TileHeightInWorldUnits)), Vector3f(0.0f, 1.0f, layerIndex));
                vertices.emplace_back(Vector3f(tilePosition.x + simScalarToFloat(MapTerrainGraphics::TileWidthInWorldUnits), 0.0f, tilePosition.z + simScalarToFloat(MapTerrainGraphics::TileHeightInWorldUnits)), Vector3f(1.0f, 1.0f, layerIndex));

                vertices.emplace_back(Vector3f(tilePosition.x + simScalarToFloat(MapTerrainGraphics::TileWidthInWorldUnits), 0.0f, tilePosition.z + simScalarToFloat(MapTerrainGraphics::TileHeightInWorldUnits)), Vector3f(1.0f, 1.0f, layerIndex));
                vertices.emplace_back(Vector3f(tilePosition.x + simScalarToFloat(MapTerrainGraphics::TileWidthInWorldUnits), 0.0f, tilePosition.z), Vector3f(1.0f, 0.0f, layerIndex));
                vertices.emplace_back(Vector3f(tilePosition.x, 0.0f, tilePosition.z), Vector3f(0.0f, 0.0f, layerIndex));
            }

            auto mesh = graphics->createTextureArrayMesh(vertices, GL_STREAM_DRAW);

            graphics->bindTextureArray(batch.first);
            graphics->drawTriangles(mesh);
        }
    }

    void RenderService::drawMapTerrain(const MapTerrainGraphics& terrain, const Vector3f& cameraPosition, float viewportWidth, float viewportHeight, const std::optional<FogOverlay>& fog)
    {
        Vector3f cameraExtents(viewportWidth / 2.0f, 0.0f, viewportHeight / 2.0f);
        auto topLeft = terrain.worldToTileCoordinate(floatToSimVector(cameraPosition - cameraExtents));
        auto bottomRight = terrain.worldToTileCoordinate(floatToSimVector(cameraPosition + cameraExtents));
        auto x1 = static_cast<unsigned int>(std::clamp<int>(topLeft.x, 0, terrain.getTiles().getWidth() - 1));
        auto y1 = static_cast<unsigned int>(std::clamp<int>(topLeft.y, 0, terrain.getTiles().getHeight() - 1));
        auto x2 = static_cast<unsigned int>(std::clamp<int>(bottomRight.x, 0, terrain.getTiles().getWidth() - 1));
        auto y2 = static_cast<unsigned int>(std::clamp<int>(bottomRight.y, 0, terrain.getTiles().getHeight() - 1));

        drawMapTerrain(terrain, x1, y1, (x2 + 1) - x1, (y2 + 1) - y1, fog);
    }

    void RenderService::fillScreen(float r, float g, float b, float a)
    {
        auto floatColor = Vector3f(r, g, b);

        // clang-format off
        std::vector<GlColoredVertex> vertices{
            {{-1.0f, -1.0f, 0.0f}, floatColor},
            {{ 1.0f, -1.0f, 0.0f}, floatColor},
            {{ 1.0f,  1.0f, 0.0f}, floatColor},

            {{ 1.0f,  1.0f, 0.0f}, floatColor},
            {{-1.0f,  1.0f, 0.0f}, floatColor},
            {{-1.0f, -1.0f, 0.0f}, floatColor},
        };
        // clang-format on

        auto mesh = graphics->createColoredMesh(vertices, GL_STREAM_DRAW);

        const auto& shader = shaders->basicColor;
        graphics->bindShader(shader.handle.get());
        graphics->setUniformMatrix(shader.mvpMatrix, Matrix4f::identity());
        graphics->setUniformFloat(shader.alpha, a);
        graphics->drawTriangles(mesh);
    }

    void RenderService::drawFlashes(GameTime currentTime, const std::vector<FlashEffect>& flashes)
    {
        const auto& shader = shaders->flashEffect;
        graphics->bindShader(shader.handle.get());

        auto quadMesh = graphics->createUnitTexturedQuad(Rectangle2f::fromTLBR(1.0f, 0.0f, 0.0f, 1.0f));

        graphics->unbindTexture();

        for (const auto& flash : flashes)
        {
            auto fractionComplete = flash.getFractionComplete(currentTime);
            if (!fractionComplete)
            {
                continue;
            }

            Vector3f snappedPosition(
                std::round(flash.position.x),
                truncateToInterval(flash.position.y, 2.0f),
                std::round(flash.position.z));

            // Convert to a model position that makes sense in the game world.
            // For standing (blocking) features we stretch y-dimension values by 2x
            // to correct for TA camera distortion.
            Matrix4f conversionMatrix = Matrix4f::scale(Vector3f(1.0f, -2.0f, 1.0f));

            auto fractionRemaining = 1.0f - *fractionComplete;

            auto modelMatrix = Matrix4f::translation(snappedPosition) * conversionMatrix * Matrix4f::scale(flash.maxRadius * fractionRemaining);

            graphics->setUniformMatrix(shader.mvpMatrix, (*viewProjectionMatrix) * modelMatrix);
            graphics->setUniformFloat(shader.intensity, flash.maxIntensity * fractionRemaining);
            graphics->setUniformVec3(shader.color, flash.color.x, flash.color.y, flash.color.z);
            graphics->drawTriangles(quadMesh);
        }
    }

    void RenderService::drawBatch(const ColoredMeshBatch& batch, const Matrix4f& vpMatrix, float alpha)
    {
        if (!batch.lines.empty() || !batch.triangles.empty())
        {
            const auto& shader = shaders->basicColor;
            graphics->bindShader(shader.handle.get());
            graphics->setUniformFloat(shader.alpha, alpha);
            graphics->setUniformMatrix(shader.mvpMatrix, vpMatrix);
            if (!batch.lines.empty())
            {

                auto mesh = graphics->createColoredMesh(batch.lines, GL_STREAM_DRAW);
                graphics->drawLines(mesh);
            }

            if (!batch.triangles.empty())
            {
                auto mesh = graphics->createColoredMesh(batch.triangles, GL_STREAM_DRAW);
                graphics->drawTriangles(mesh);
            }
        }
    }

    void RenderService::drawUnitMeshBatch(const UnitMeshBatch& batch, float seaLevel, TextureIdentifier shadeTableTexture)
    {
        // Finished models first: a nanoframe's see-through parts still write
        // depth, so drawing it after the lab it sits in leaves the lab's bay
        // visible through it instead of the ground.
        if (!batch.meshes.empty())
        {
            const auto& textureShader = shaders->unitTexture;
            graphics->bindShader(textureShader.handle.get());
            graphics->setUniformFloat(textureShader.seaLevel, seaLevel);
            graphics->setUniformFloat(textureShader.alpha, 1.0f);

            // Three samplers, one per texture unit: the colour atlas on 0, the
            // palette-index copy of it on 1 and the shade table on 2. Every
            // sampler has to be told which unit it is on -- one left unset
            // reads zero and lands on slot 0 with the colour atlas, which is
            // what the terrain's fog sampler explains at length above. The
            // shade table is bound once for the whole block because it is the
            // same image for every mesh. Each pass through the loop ends on
            // slot 0, because every other bindTexture in the renderer assumes
            // slot 0 is the current one.
            graphics->setUniformInt(textureShader.paletteIndexSampler, 1);
            graphics->setUniformInt(textureShader.shadeTableSampler, 2);
            graphics->setActiveTextureSlot2();
            graphics->bindTexture(shadeTableTexture);

            for (const auto& m : batch.meshes)
            {
                graphics->setUniformMatrix(textureShader.mvpMatrix, m.mvpMatrix);
                graphics->setUniformMatrix(textureShader.modelMatrix, m.modelMatrix);
                graphics->setUniformFloat(textureShader.shadeStrength, m.shadeStrength);
                graphics->setActiveTextureSlot1();
                graphics->bindTexture(m.paletteIndexTexture);
                graphics->setActiveTextureSlot0();
                graphics->bindTexture(m.texture);
                graphics->drawTriangles(*m.mesh);
            }
        }

        if (!batch.buildingMeshes.empty())
        {
            const auto& buildShader = shaders->unitBuild;
            graphics->bindShader(buildShader.handle.get());
            graphics->setUniformFloat(buildShader.seaLevel, seaLevel);
            graphics->setUniformInt(buildShader.paletteIndexSampler, 1);
            graphics->setUniformInt(buildShader.shadeTableSampler, 2);
            graphics->setActiveTextureSlot2();
            graphics->bindTexture(shadeTableTexture);
            for (const auto& m : batch.buildingMeshes)
            {
                graphics->setUniformMatrix(buildShader.mvpMatrix, m.mvpMatrix);
                graphics->setUniformMatrix(buildShader.modelMatrix, m.modelMatrix);
                graphics->setUniformFloat(buildShader.unitY, m.unitY);
                graphics->setUniformFloat(buildShader.unitHeight, m.unitHeight);
                graphics->setUniformFloat(buildShader.shadeStrength, m.shadeStrength);
                graphics->setUniformFloat(buildShader.buildRatio, m.buildRatio);
                graphics->setUniformInt(buildShader.aboveMode, static_cast<int>(m.aboveMode));
                graphics->setUniformInt(buildShader.bandMode, static_cast<int>(m.bandMode));
                graphics->setUniformInt(buildShader.belowMode, static_cast<int>(m.belowMode));
                graphics->setUniformVec3(buildShader.buildColorA, m.buildColorA.x, m.buildColorA.y, m.buildColorA.z);
                graphics->setUniformVec3(buildShader.buildColorB, m.buildColorB.x, m.buildColorB.y, m.buildColorB.z);

                graphics->setActiveTextureSlot1();
                graphics->bindTexture(m.paletteIndexTexture);
                graphics->setActiveTextureSlot0();
                graphics->bindTexture(m.texture);
                graphics->drawTriangles(*m.mesh);
            }
        }

        // Cloaked units last of all, so what stands behind them has already
        // been painted and can show through. The original gets that for free:
        // it composites the whole unit into a bitmap of its own and blits that
        // over the finished scene through the ALPHA TABLE, one average per
        // covered pixel. Blending the model straight into the frame would
        // instead average a pixel again for every polygon of the unit stacked
        // over it, and the thick parts would come out nearly solid, so lay the
        // depth down in a pass of its own first and then let only the nearest
        // fragment through.
        if (!batch.cloakedMeshes.empty())
        {
            const auto& textureShader = shaders->unitTexture;
            graphics->bindShader(textureShader.handle.get());
            graphics->setUniformFloat(textureShader.seaLevel, seaLevel);
            graphics->setUniformInt(textureShader.paletteIndexSampler, 1);
            graphics->setUniformInt(textureShader.shadeTableSampler, 2);
            graphics->setActiveTextureSlot2();
            graphics->bindTexture(shadeTableTexture);

            graphics->disableColorBuffer();
            for (const auto& m : batch.cloakedMeshes)
            {
                graphics->setUniformMatrix(textureShader.mvpMatrix, m.mvpMatrix);
                graphics->setUniformMatrix(textureShader.modelMatrix, m.modelMatrix);
                graphics->setUniformFloat(textureShader.shadeStrength, m.shadeStrength);
                graphics->setActiveTextureSlot1();
                graphics->bindTexture(m.paletteIndexTexture);
                graphics->setActiveTextureSlot0();
                graphics->bindTexture(m.texture);
                graphics->drawTriangles(*m.mesh);
            }
            graphics->enableColorBuffer();

            graphics->setUniformFloat(textureShader.alpha, CloakBlendFactor);
            graphics->useDepthTestEqual();
            graphics->disableDepthWrites();
            for (const auto& m : batch.cloakedMeshes)
            {
                graphics->setUniformMatrix(textureShader.mvpMatrix, m.mvpMatrix);
                graphics->setUniformMatrix(textureShader.modelMatrix, m.modelMatrix);
                graphics->setUniformFloat(textureShader.shadeStrength, m.shadeStrength);
                graphics->setActiveTextureSlot1();
                graphics->bindTexture(m.paletteIndexTexture);
                graphics->setActiveTextureSlot0();
                graphics->bindTexture(m.texture);
                graphics->drawTriangles(*m.mesh);
            }
            graphics->enableDepthWrites();
            graphics->enableDepthTest();
            graphics->setUniformFloat(textureShader.alpha, 1.0f);
        }
    }

    void RenderService::drawUnitMaskBatch(const std::vector<UnitTextureMeshRenderInfo>& meshes, float maskAlpha)
    {
        if (meshes.empty())
        {
            return;
        }

        const auto& shader = shaders->unitMask;
        graphics->bindShader(shader.handle.get());
        graphics->setUniformInt(shader.paletteIndexSampler, 1);
        graphics->setUniformFloat(shader.maskAlpha, maskAlpha);

        for (const auto& m : meshes)
        {
            graphics->setUniformMatrix(shader.mvpMatrix, m.mvpMatrix);
            // Slot 1 is the index atlas, slot 0 the colour atlas the alpha
            // test reads. Same pairing the unit shader uses.
            graphics->setActiveTextureSlot1();
            graphics->bindTexture(m.paletteIndexTexture);
            graphics->setActiveTextureSlot0();
            graphics->bindTexture(m.texture);
            graphics->drawTriangles(*m.mesh);
        }
    }

    void RenderService::drawUnitShadowMeshBatch(const UnitShadowMeshBatch& batch)
    {
        if (batch.meshes.empty())
        {
            return;
        }

        graphics->enableStencilBuffer();
        graphics->clearStencilBuffer();
        graphics->useStencilBufferForWrites();
        graphics->disableColorBuffer();

        const auto& shader = shaders->unitShadow;
        graphics->bindShader(shader.handle.get());

        for (const auto& m : batch.meshes)
        {
            graphics->setUniformFloat(shader.groundHeight, m.groundHeight);
            graphics->setUniformBool(shader.projected, m.projected);
            graphics->setUniformFloat(shader.shadowOriginY, m.shadowOriginY);
            graphics->setUniformMatrix(shader.vpMatrix, m.vpMatrix);
            graphics->setUniformMatrix(shader.modelMatrix, m.modelMatrix);

            graphics->bindTexture(m.texture);
            graphics->drawTriangles(*m.mesh);
        }

        if (!batch.cutouts.empty())
        {
            // Erase the shadow wherever these models sit on screen, so it
            // only shows where it falls outside them.
            graphics->useStencilBufferForClears();
            const auto& textureShader = shaders->unitTexture;
            graphics->bindShader(textureShader.handle.get());
            graphics->setUniformFloat(textureShader.seaLevel, 0.0f);
            graphics->setUniformFloat(textureShader.alpha, 1.0f);
            graphics->setUniformFloat(textureShader.shadeStrength, 0.0f);
            // A strength of zero means the shader never reaches the table, so
            // this pass binds neither of the other two textures. It still says
            // which unit each sampler is on, so that the program's samplers
            // are never all sitting on slot 0 with the colour atlas under
            // them -- an invariant worth keeping true everywhere rather than
            // only where it currently matters.
            graphics->setUniformInt(textureShader.paletteIndexSampler, 1);
            graphics->setUniformInt(textureShader.shadeTableSampler, 2);
            for (const auto& m : batch.cutouts)
            {
                graphics->setUniformMatrix(textureShader.mvpMatrix, m.mvpMatrix);
                graphics->setUniformMatrix(textureShader.modelMatrix, m.modelMatrix);
                graphics->bindTexture(m.texture);
                graphics->drawTriangles(*m.mesh);
            }
        }

        graphics->useStencilBufferAsMask();
        graphics->enableColorBuffer();

        fillScreen(0.0f, 0.0f, 0.0f, 0.7f);

        graphics->enableColorBuffer();
        graphics->disableStencilBuffer();
    }

    void RenderService::drawSpriteBatch(const SpriteBatch& batch)
    {
        const auto& shader = shaders->basicTexture;
        graphics->bindShader(shader.handle.get());

        for (const auto& s : batch.sprites)
        {
            float alpha = s.translucent ? 0.5f : 1.0f;
            graphics->bindTexture(s.sprite->texture.get());
            graphics->setUniformMatrix(shader.mvpMatrix, s.mvpMatrix);
            graphics->setUniformVec4(shader.tint, 1.0f, 1.0f, 1.0f, alpha);
            graphics->setUniformFloat(shader.desaturate, s.fogged ? 1.0f : 0.0f);
            graphics->drawTriangles(*s.sprite->mesh);
        }
    }
    void RenderService::drawLineLoopsBatch(const ColoredMeshesBatch& batch)
    {
        const auto& shader = shaders->basicColor;
        graphics->bindShader(shader.handle.get());
        for (const auto& m : batch.meshes)
        {
            graphics->setUniformMatrix(shader.mvpMatrix, m.mvpMatrix);
            graphics->setUniformFloat(shader.alpha, 1.0f);
            graphics->drawLineLoop(*m.mesh);
        }
    }
}
