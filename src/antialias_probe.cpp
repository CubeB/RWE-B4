// Real-GL regression checks for AA-ELIGIBILITY-01 and the RWE-only mask
// machinery documented in docs/ANTIALIASING-MASK-VALIDATION.md.
#include "antialias_probe.h"
#include <glad/gl.h>
#include <rwe/TextureService.h>
#include <rwe/io/gaf/GafArchive.h>
#include <rwe/util/SpanStream.h>
#include <rwe/render/WorldRaster.h>
#include <rwe/mesh_util.h>
#include <rwe/cob/CobExecutionContext.h>
#include <rwe/RenderService.h>
#include <rwe/io/tnt/TntArchive.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <set>
#include <stdexcept>

namespace rwe
{
    namespace
    {
        GLuint shader(GLenum kind, const std::string& source)
        {
            auto id = glCreateShader(kind);
            auto text = source.c_str();
            glShaderSource(id, 1, &text, nullptr);
            glCompileShader(id);
            GLint ok;
            glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[4096]; glGetShaderInfoLog(id, sizeof(log), nullptr, log);
                throw std::runtime_error(log);
            }
            return id;
        }

        GLuint program(const std::string& fragment, const std::string& outputs = "")
        {
            auto vs = shader(GL_VERTEX_SHADER, "#version 150\n"
                "out vec2 fragTexCoord; out float height; out float shadeLevel; uniform mat4 raster;\n"
                "void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
                "fragTexCoord=p; height=100.0; shadeLevel=0.0;"
                "gl_Position=raster*vec4(p*2.0-1.0,0,1); }\n");
            auto fs = shader(GL_FRAGMENT_SHADER, fragment);
            auto id = glCreateProgram();
            glAttachShader(id, vs); glAttachShader(id, fs);
            glBindFragDataLocation(id, 0, "outColor");
            glBindFragDataLocation(id, 1, "outMask");
            glLinkProgram(id);
            GLint ok; glGetProgramiv(id, GL_LINK_STATUS, &ok);
            if (!ok) throw std::runtime_error("probe shader link failed " + outputs);
            glDeleteShader(vs); glDeleteShader(fs);
            glUseProgram(id);
            auto identity = Matrix4f::identity();
            glUniformMatrix4fv(glGetUniformLocation(id, "raster"), 1, GL_FALSE, identity.data);
            return id;
        }

        std::string read(const std::string& path)
        {
            std::ifstream in(path);
            if (!in) throw std::runtime_error("Missing " + path);
            std::ostringstream out; out << in.rdbuf(); return out.str();
        }

        void scalar(GLuint p, const char* name, float value)
        { glUniform1f(glGetUniformLocation(p, name), value); }
        void sampler(GLuint p, const char* name, int slot, GLuint texture)
        {
            glUniform1i(glGetUniformLocation(p, name), slot);
            glActiveTexture(GL_TEXTURE0 + slot); glBindTexture(GL_TEXTURE_2D, texture);
        }
        std::vector<unsigned char> pixels(int w, int h, GLenum attachment)
        {
            std::vector<unsigned char> result(w * h * 4);
            glReadBuffer(attachment);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
            return result;
        }
    }

    int runAntialiasProbe(GraphicsContext& graphics, TextureService& textures, AbstractVirtualFileSystem& vfs)
    {
        int failures = 0;
        auto check = [&](bool ok, const std::string& label) {
            std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
            failures += !ok;
        };
        std::cout << "GPU " << glGetString(GL_RENDERER) << '\n';
        GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
        auto world = graphics.createFrameBuffer(8, 8);
        auto mask = graphics.createEmptyTexture(8, 8);
        graphics.enableBlending();
        graphics.attachFrameBufferMaskBuffer(world.frameBuffer.get(), mask.get());
        auto output = graphics.createFrameBuffer(4, 4);
        auto zero = graphics.createTexture(1, 1, std::vector<Color>{Color(0, 0, 0, 0)});
        auto labels = program("#version 150\nuniform vec4 label; out vec4 outColor; out vec4 outMask;"
            "void main(){outColor=vec4(0.2,0.4,0.6,0.5);outMask=label;}");
        auto post = program(read("shaders/worldPost.frag"));
        auto sprite = program(read("shaders/basicTextureMasked.frag"));
        auto model = program(read("shaders/unitTexture.frag"));
        auto drawLabel = [&](float r, float g, float b, float a) {
            glUseProgram(labels);
            glUniform4f(glGetUniformLocation(labels, "label"), r, g, b, a);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        };
        auto begin = [&] {
            glBindVertexArray(vao);
            graphics.bindFrameBuffer(world.frameBuffer.get());
            glViewport(0, 0, 8, 8);
            // Reproduce the UI backend's global blend-state restore each frame.
            glEnable(GL_BLEND);
            graphics.useDualDrawBuffers();
            glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        };
        for (int frame = 0; frame < 3; ++frame)
        {
            begin();
            for (int overdraw = 0; overdraw < 8; ++overdraw) drawLabel(0, 1, 0, 0.5f);
            auto terrain = pixels(8, 8, GL_COLOR_ATTACHMENT1);
            std::cout << "terrain mask g/a=" << int(terrain[1]) << '/' << int(terrain[3]) << '\n';
            check(terrain[1] == 255 && std::abs(int(terrain[3]) - 128) <= 1, "terrain category survives overdraw/frame " + std::to_string(frame));
            drawLabel(100.0f / 255.0f, 0, 0, 0.7f);
            auto live = pixels(8, 8, GL_COLOR_ATTACHMENT1);
            check(live[0] == 100 && live[1] == 0 && std::abs(int(live[3]) - 179) <= 1,
                "live-piece category and palette index replace terrain");
            drawLabel(100.0f / 255.0f, 0, 0, 0.5f);
            auto unit = pixels(8, 8, GL_COLOR_ATTACHMENT1);
            check(unit[1] == 0 && std::abs(int(unit[3]) - 128) <= 1, "mobile category replaces previous geometry");
            auto color = pixels(8, 8, GL_COLOR_ATTACHMENT0);
            check(color[0] > 45 && color[0] < 51, "colour attachment retains alpha blending");
        }

        // Distinct supersamples let the resolve distinguish a sharp sample
        // from the four-sample average without depending on visual judgement.
        std::vector<Color> colors(64);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                colors[y * 8 + x] = Color((x % 2) * 200, (y % 2) * 200, 0);
        auto pattern = graphics.createTexture(8, 8, colors);
        glBindTexture(GL_TEXTURE_2D, pattern.get().value);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        auto resolve = [&](float units, GLuint source = 0) {
            glBindVertexArray(vao);
            graphics.bindFrameBuffer(output.frameBuffer.get()); graphics.useSingleDrawBuffer();
            glViewport(0, 0, 4, 4); glUseProgram(post);
            sampler(post, "screenTexture", 0, source ? source : pattern.get().value);
            sampler(post, "dodgeMask", 1, zero.get().value);
            sampler(post, "buildingMask", 2, mask.get().value);
            scalar(post, "gamma", 1); scalar(post, "selectiveAntiAlias", 1);
            scalar(post, "antiAliasUnits", units); scalar(post, "haloStrength", 0);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            return pixels(4, 4, GL_COLOR_ATTACHMENT0);
        };
        struct Case { const char* name; float green, blue, alpha; bool smooth; };
        for (float units : {0.0f, 1.0f})
        {
            for (auto c : {Case{"terrain",1,0,.5f,false}, Case{"cached building",0,0,1,true},
                     Case{"live piece",0,0,.7f,false}, Case{"billboard",0,1,.5f,false},
                     Case{"mobile",0,0,.5f,units > 0}})
            {
                begin(); drawLabel(0, c.green, c.blue, c.alpha);
                auto value = resolve(units);
                check(std::abs(int(value[0]) - (c.smooth ? 100 : 0)) <= 1,
                    std::string(c.name) + " resolve, Units " + (units > 0 ? "Smooth" : "Sharp"));
            }
        }

        for (float units : {0.0f, 1.0f})
        {
            for (bool isBillboard : {false, true})
            {
                begin(); drawLabel(0, 0, 0, 1);
                glEnable(GL_SCISSOR_TEST); glScissor(0, 0, 1, 1);
                drawLabel(0, 0, isBillboard ? 1 : 0, .5f);
                glDisable(GL_SCISSOR_TEST);
                auto value = resolve(units);
                check(std::abs(int(value[0]) - ((!isBillboard && units > 0) ? 100 : 0)) <= 1,
                    std::string(isBillboard ? "billboard" : "mobile") + " occludes cached building, Units "
                        + (units > 0 ? "Smooth" : "Sharp"));
            }
        }

        // Execute the production shading fragment shader against the actual
        // shipped palette and SHD file, independently of the postprocess.
        auto palette = *vfs.readPaletteOrPcx("PALETTE");
        auto shd = vfs.readFileOrThrow("palettes/PALETTE.SHD");
        std::vector<Color> table;
        for (auto index : shd) table.push_back(palette[static_cast<unsigned char>(index)]);
        auto shadeTexture = graphics.createTexture(256, 32, table);
        unsigned int index = 0;
        while (index < 255 && palette[index].r < 100) ++index;
        auto colorTexture = graphics.createTexture(1, 1, std::vector<Color>{palette[index]});
        auto indexTexture = graphics.createTexture(1, 1, std::vector<Color>{Color(index, 0, 0)});
        for (float strength : {0.0f, 0.4f, 1.0f})
        {
            begin(); glUseProgram(model);
            sampler(model, "textureSampler", 0, colorTexture.get().value);
            sampler(model, "paletteIndexSampler", 1, indexTexture.get().value);
            sampler(model, "shadeTableSampler", 2, shadeTexture.get().value);
            scalar(model, "alpha", 1); scalar(model, "maskValue", 1);
            scalar(model, "seaLevel", -100); scalar(model, "cutBelowY", -100);
            scalar(model, "shadeStrength", strength);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            auto actual = pixels(8, 8, GL_COLOR_ATTACHMENT0);
            auto shaded = table[index]; auto raw = palette[index];
            check(std::abs(int(actual[0]) - std::lround(raw.r * (1-strength) + shaded.r * strength)) <= 1
                && std::abs(int(actual[1]) - std::lround(raw.g * (1-strength) + shaded.g * strength)) <= 1,
                "production building shading at strength " + std::to_string(strength));
            for (float units : {0.0f, 1.0f})
            {
                auto resolved = resolve(units, world.texture.get().value);
                check(std::equal(actual.begin(), actual.begin() + 3, resolved.begin()),
                    "AA resolve preserves building shading, Units " + std::to_string(units));
            }
        }

        // Find a real shipped tree frame rather than manufacturing an alpha
        // silhouette. Match either archive or entry name, print the fixture.
        std::shared_ptr<Sprite> tree;
        for (auto file : vfs.getFileNames("anims", ".gaf"))
        {
            auto path = "anims/" + file;
            auto bytes = vfs.readFileOrThrow(path);
            SpanStream stream(bytes.data(), bytes.size()); GafArchive gaf(&stream);
            for (auto& entry : gaf.entries())
            {
                auto key = file + "/" + entry.name;
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::toupper(c); });
                if (key.find("TREE") == std::string::npos) continue;
                tree = textures.getGafEntry(path, entry.name)->sprites.at(0);
                std::cout << "TREE FIXTURE " << path << "/" << entry.name << '\n';
                break;
            }
            if (tree) break;
        }
        check(bool(tree), "shipped tree fixture found");
        if (tree)
        {
            begin(); drawLabel(0, 1, 0, .5f);
            glBindVertexArray(vao);
            glUseProgram(sprite); sampler(sprite, "textureSampler", 0, tree->texture.get().value);
            scalar(sprite, "maskValue", .5f); scalar(sprite, "desaturate", 0);
            glUniform4f(glGetUniformLocation(sprite, "tint"), 1, 1, 1, 1);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            auto values = pixels(8, 8, GL_COLOR_ATTACHMENT1);
            int opaque = 0, transparent = 0;
            for (int i = 0; i < 64; ++i)
            {
                opaque += values[i*4+2] == 255 && std::abs(int(values[i*4+3]) - 128) <= 1;
                transparent += values[i*4+1] == 255 && values[i*4+2] == 0;
            }
            check(opaque > 0 && transparent > 0 && opaque + transparent == 64,
                "shipped tree marks only its opaque silhouette sharp");
        }

        // SOLAR-SHADING-01: execute the shipped Create in RWE's VM. Stop at
        // its first environment-dependent yield; flags precede that point.
        auto cobBytes = vfs.readFileOrThrow("scripts/CORSOLAR.cob");
        SpanStream cobStream(cobBytes.data(), cobBytes.size());
        auto script = parseCob(cobStream);
        CobEnvironment environment(&script);
        auto creation = environment.createNonScheduledThread("Create", {});
        check(bool(creation), "shipped CORSOLAR Create exists");
        std::set<unsigned int> noShade, noCache;
        if (creation)
        {
            CobExecutionContext context(&environment, &*creation);
            for (int step = 0; step < 10000; ++step)
            {
                auto status = context.execute();
                if (auto command = std::get_if<CobEnvironment::PieceCommandStatus>(&status))
                {
                    if (std::holds_alternative<CobEnvironment::PieceCommandStatus::DisableShading>(command->command))
                        noShade.insert(command->piece);
                    if (std::holds_alternative<CobEnvironment::PieceCommandStatus::DisableCaching>(command->command))
                        noCache.insert(command->piece);
                }
                else if (!std::holds_alternative<CobEnvironment::SignalStatus>(status)
                    && !std::holds_alternative<CobEnvironment::SetQueryStatus>(status)) break;
            }
        }
        auto base = std::find(script.pieces.begin(), script.pieces.end(), "base");
        check(noShade.size() == 10 && noShade.size() == script.pieces.size(),
            "RWE VM executes shipped CORSOLAR shade-disable commands for all ten pieces");
        check(noCache.size() == 9 && base != script.pieces.end()
                && !noCache.count(static_cast<unsigned int>(base - script.pieces.begin())),
            "RWE VM keeps only CORSOLAR base cached");

        // Use the reported building's shipped geometry and the actual vertex
        // shader, not just a constant shade row. A constant material isolates
        // geometric lighting from artwork; this is not an original-game oracle.
        auto modelBytes = vfs.readFileOrThrow("objects3d/CORSOLAR.3do");
        SpanStream modelStream(modelBytes.data(), modelBytes.size());
        auto objects = parse3doObjects(modelStream, modelStream.tellg());
        std::vector<GlTexturedNormalVertex> vertices;
        std::function<void(const _3do::Object&, Vector3f)> append = [&](const auto& object, Vector3f origin) {
            origin = origin + vertexToVector(_3do::Vertex{object.x, object.y, object.z});
            auto mesh = meshFrom3do({}, {}, std::vector<Vector2f>(256, Vector2f(.5f, .5f)), object);
            for (const auto& face : mesh.faces)
                for (const auto& vertex : {face.a, face.b, face.c})
                    vertices.emplace_back(vertex.position + origin, Vector2f(.5f, .5f), vertex.normal);
            for (const auto& child : object.children) append(child, origin);
        };
        append(objects.at(0), Vector3f(0, 0, 0));
        auto geometry = graphics.createTexturedNormalMesh(vertices, GL_STATIC_DRAW);
        auto vertexShader = graphics.compileVertexShader(read("shaders/unitTexture.vert"));
        auto fragmentShader = graphics.compileFragmentShader(read("shaders/unitTexture.frag"));
        auto productionModel = graphics.linkShaderProgram(vertexShader.get(), fragmentShader.get(),
            {{"position", 0}, {"texCoord", 1}, {"normal", 2}});
        auto solarTarget = graphics.createFrameBuffer(64, 64);
        auto view = Matrix4f::cabinetProjection(0, .5f)
            * Matrix4f::rotationToAxes(Vector3f(1,0,0), Vector3f(0,0,-1), Vector3f(0,1,0));
        float left = 1e6f, right = -1e6f, bottom = 1e6f, top = -1e6f;
        for (const auto& v : vertices)
        {
            auto p = view * Vector3f(v.x, v.y, v.z);
            left = std::min(left, p.x); right = std::max(right, p.x);
            bottom = std::min(bottom, p.y); top = std::max(top, p.y);
        }
        auto mvp = Matrix4f::orthographicProjection(left-2, right+2, bottom-2, top+2, -1000, 1000) * view;
        auto identity = Matrix4f::identity();
        std::vector<unsigned char> unshaded;
        for (float strength : {0.0f, .4f})
        {
            graphics.bindFrameBuffer(solarTarget.frameBuffer.get()); graphics.useSingleDrawBuffer();
            glViewport(0,0,64,64); glClear(GL_COLOR_BUFFER_BIT);
            auto p = productionModel.get().value; glUseProgram(p);
            sampler(p, "textureSampler", 0, colorTexture.get().value);
            sampler(p, "paletteIndexSampler", 1, indexTexture.get().value);
            sampler(p, "shadeTableSampler", 2, shadeTexture.get().value);
            glUniformMatrix4fv(glGetUniformLocation(p, "modelMatrix"), 1, GL_FALSE, identity.data);
            glUniformMatrix4fv(glGetUniformLocation(p, "mvpMatrix"), 1, GL_FALSE, mvp.data);
            scalar(p, "seaLevel", -1000); scalar(p, "cutBelowY", -1000);
            scalar(p, "alpha", 1); scalar(p, "shadeStrength", strength); scalar(p, "maskValue", 1);
            graphics.drawTriangles(geometry);
            auto actual = pixels(64, 64, GL_COLOR_ATTACHMENT0);
            if (strength == 0) unshaded = actual;
            else
            {
                int changed = 0;
                for (unsigned int i = 0; i < actual.size(); i += 4)
                    changed += std::abs(int(actual[i]) - int(unshaded[i])) > 2;
                std::cout << "CORSOLAR shaded pixels changed: " << changed << '\n';
                check(changed > 20, "shipped CORSOLAR normals change production vertex/fragment shading");
            }
        }
        glBindVertexArray(vao);

        // Compare the sharp supersample with an actual native raster. A
        // non-integral texture size exercises both camera/sample parities.
        auto raster = program("#version 150\nin vec2 fragTexCoord; uniform sampler2D source;"
            "out vec4 outColor; void main(){outColor=texture(source,fragTexCoord);}");
        std::vector<Color> detail(19 * 23);
        for (unsigned int i = 0; i < detail.size(); ++i)
            detail[i] = Color((i * 37) % 256, (i * 83) % 256, (i * 17) % 256);
        auto detailTexture = graphics.createTexture(19, 23, detail);
        glBindTexture(GL_TEXTURE_2D, detailTexture.get().value);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        auto native = graphics.createFrameBuffer(8, 8);
        auto supersampled = graphics.createFrameBuffer(16, 16);
        auto renderRaster = [&](FrameBufferIdentifier fbo, int scale, bool align, float pan) {
            graphics.bindFrameBuffer(fbo); graphics.useSingleDrawBuffer();
            glViewport(0, 0, 8 * scale, 8 * scale); glUseProgram(raster);
            sampler(raster, "source", 0, detailTexture.get().value);
            auto matrix = Matrix4f::translation(Vector3f(pan, pan * .7f, 0));
            if (align) matrix = alignWorldRaster(matrix, 8, 8, scale);
            glUniformMatrix4fv(glGetUniformLocation(raster, "raster"), 1, GL_FALSE, matrix.data);
            glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(GL_TRIANGLES, 0, 3);
            return pixels(8 * scale, 8 * scale, GL_COLOR_ATTACHMENT0);
        };
        int oldMismatches = 0, correctedMismatches = 0;
        for (float pan : {0.0f, -.031f, -.083f, -.137f})
        {
            auto expected = renderRaster(native.frameBuffer.get(), 1, false, pan);
            for (bool align : {false, true})
            {
                auto actual = renderRaster(supersampled.frameBuffer.get(), 2, align, pan);
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        if (!std::equal(expected.begin() + (y*8+x)*4, expected.begin() + (y*8+x)*4+3,
                                actual.begin() + ((y*2)*16+x*2)*4))
                        {
                            if (align) ++correctedMismatches; else ++oldMismatches;
                        }
            }
        }
        check(oldMismatches > 0, "old quarter-pixel phase error reproduced");
        check(correctedMismatches == 0, "sharp AA samples match native raster across camera phases");

        // AA-PHASE-01 follow-up: the preceding fixture intentionally excludes
        // mipmaps. Exercise actual TNT tiles and the production terrain path.
        auto mapBytes = vfs.readFileOrThrow("maps/Coast To Coast.tnt");
        SpanStream mapStream(mapBytes.data(), mapBytes.size());
        TntArchive tnt(&mapStream);
        std::vector<Color> tilePixels;
        tnt.readTiles([&](const char* tile) {
            if (tilePixels.size() >= 16 * 32 * 32) return;
            for (int i = 0; i < 32*32; ++i) tilePixels.push_back(palette[static_cast<unsigned char>(tile[i])]);
        });
        SharedTextureArrayHandle tileArray(graphics.createTextureArray(32,32,5,tilePixels));
        std::vector<TextureArrayRegion> regions;
        for (unsigned int i = 0; i < tilePixels.size() / (32*32); ++i) regions.emplace_back(tileArray, i);
        Grid<std::size_t> tileGrid(16,16,0);
        for (unsigned int y = 0; y < 16; ++y)
            for (unsigned int x = 0; x < 16; ++x) tileGrid.set(x,y,(x+y*3)%regions.size());
        MapTerrainGraphics terrain(std::move(regions), std::move(tileGrid));
        auto terrainShaders = ShaderService::createShaderService(graphics);
        auto nativeTerrain = graphics.createFrameBuffer(128,96);
        auto doubleTerrain = graphics.createFrameBuffer(256,192);
        auto resolvedTerrain = graphics.createFrameBuffer(128,96);
        auto nativeGroundMask = graphics.createEmptyTexture(128,96);
        auto doubleGroundMask = graphics.createEmptyTexture(256,192);
        graphics.attachFrameBufferMaskBuffer(nativeTerrain.frameBuffer.get(), nativeGroundMask.get());
        graphics.attachFrameBufferMaskBuffer(doubleTerrain.frameBuffer.get(), doubleGroundMask.get());
        auto terrainRaster = [&](int scale, float zoom, int pan, bool copyNative = false) {
            if (copyNative)
            {
                graphics.blitFrameBufferColorAndMask(nativeTerrain.frameBuffer.get(), doubleTerrain.frameBuffer.get(),128,96,256,192);
                return pixels(256,192,GL_COLOR_ATTACHMENT0);
            }
            graphics.bindFrameBuffer(scale == 1 ? nativeTerrain.frameBuffer.get() : doubleTerrain.frameBuffer.get());
            graphics.useDualDrawBuffers(); glViewport(0,0,128*scale,96*scale);
            glClear(GL_COLOR_BUFFER_BIT);
            auto view = Matrix4f::rotationToAxes(Vector3f(1,0,0),Vector3f(0,0,-1),Vector3f(0,1,0))
                * Matrix4f::translation(Vector3f(-float(pan),0,-float(pan)));
            auto projection = Matrix4f::orthographicProjection(-64/zoom,64/zoom,-48/zoom,48/zoom,-1000,1000)
                * Matrix4f::cabinetProjection(0,.5f);
            auto matrix = alignWorldRaster(projection*view,128,96,scale);
            RenderService service(&graphics,&terrainShaders,&matrix);
            service.drawMapTerrain(terrain,0u,0u,16u,16u);
            return pixels(128*scale,96*scale,GL_COLOR_ATTACHMENT0);
        };
        int oldTerrainMismatches = 0;
        for (float zoom : {.5f,.75f,1.0f,1.01f,1.25f,2.0f})
        {
            int mismatch = 0, copiedMismatch = 0, maskMismatch = 0, resolvedMismatch = 0;
            for (int pan : {0,1,17})
            {
                auto expected = terrainRaster(1,zoom,pan);
                auto expectedMask = pixels(128,96,GL_COLOR_ATTACHMENT1);
                auto actual = terrainRaster(2,zoom,pan);
                auto copied = terrainRaster(2,zoom,pan,true);
                auto copiedMask = pixels(256,192,GL_COLOR_ATTACHMENT1);
                for (int y = 0; y < 96; ++y)
                    for (int x = 0; x < 128; ++x)
                    {
                        mismatch += !std::equal(expected.begin()+(y*128+x)*4,expected.begin()+(y*128+x)*4+3,
                            actual.begin()+((y*2)*256+x*2)*4);
                        for (int dy : {0,1})
                            for (int dx : {0,1})
                            {
                                const auto target = ((y*2+dy)*256+x*2+dx)*4;
                                copiedMismatch += !std::equal(expected.begin()+(y*128+x)*4,expected.begin()+(y*128+x)*4+3,
                                    copied.begin()+target);
                                maskMismatch += !std::equal(expectedMask.begin()+(y*128+x)*4,expectedMask.begin()+(y*128+x)*4+4,
                                    copiedMask.begin()+target);
                            }
                    }
                for (float units : {0.0f,1.0f})
                {
                    graphics.bindFrameBuffer(resolvedTerrain.frameBuffer.get());
                    graphics.useSingleDrawBuffer(); glViewport(0,0,128,96);
                    glBindVertexArray(vao); glUseProgram(post);
                    sampler(post,"screenTexture",0,doubleTerrain.texture.get().value);
                    sampler(post,"buildingMask",2,doubleGroundMask.get().value);
                    sampler(post,"dodgeMask",1,zero.get().value);
                    scalar(post,"selectiveAntiAlias",1); scalar(post,"antiAliasUnits",units);
                    scalar(post,"gamma",1); scalar(post,"haloStrength",0);
                    glDrawArrays(GL_TRIANGLES,0,3);
                    auto resolved = pixels(128,96,GL_COLOR_ATTACHMENT0);
                    for (unsigned int i = 0; i < expected.size(); i += 4)
                        resolvedMismatch += !std::equal(expected.begin()+i,expected.begin()+i+3,resolved.begin()+i);
                }
            }
            std::cout << "TNT native/AA differing pixels at zoom " << zoom << ": " << mismatch << '\n';
            oldTerrainMismatches += mismatch;
            check(copiedMismatch == 0,"native terrain colours unchanged by AA at zoom " + std::to_string(zoom));
            check(maskMismatch == 0,"native terrain categories copied exactly at zoom " + std::to_string(zoom));
            check(resolvedMismatch == 0,"final terrain pixels unchanged with Units Sharp/Smooth at zoom " + std::to_string(zoom));
        }
        check(oldTerrainMismatches > 0,"old production terrain AA grid reproduced");
        auto error = glGetError();
        std::cout << "OpenGL error: " << error << '\n';
        check(error == GL_NO_ERROR, "no OpenGL errors");
        glDeleteProgram(raster);
        for (auto p : {labels, post, sprite, model}) glDeleteProgram(p);
        glDeleteVertexArrays(1, &vao);
        std::cout << "Antialias probe failures: " << failures << '\n';
        return failures ? 1 : 0;
    }
}
