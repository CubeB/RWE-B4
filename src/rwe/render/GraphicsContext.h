#pragma once

#include <GL/glew.h>
#include <SDL3/SDL.h>
#include <memory>
#include <rwe/ColorPalette.h>
#include <rwe/Mesh.h>
#include <rwe/geometry/CollisionMesh.h>
#include <rwe/grid/Grid.h>
#include <rwe/math/Vector3f.h>
#include <optional>
#include <rwe/render/FrameBufferHandle.h>
#include <rwe/render/GlMesh.h>
#include <rwe/render/RenderBufferHandle.h>
#include <rwe/render/ShaderHandle.h>
#include <rwe/render/ShaderProgramHandle.h>
#include <rwe/render/Sprite.h>
#include <rwe/render/SpriteSeries.h>
#include <rwe/render/TextureArrayHandle.h>
#include <rwe/render/TextureHandle.h>
#include <rwe/render/UniformLocation.h>
#include <rwe/sim/MapFeature.h>
#include <rwe/sim/MapTerrain.h>

namespace rwe
{
#pragma pack(1)
    struct GlTexturedVertex
    {
        GLfloat x;
        GLfloat y;
        GLfloat z;
        GLfloat u;
        GLfloat v;

        GlTexturedVertex() = default;
        GlTexturedVertex(const Vector3f& pos, const Vector2f& texCoord);
    };

    struct GlTextureArrayVertex
    {
        GLfloat x;
        GLfloat y;
        GLfloat z;
        GLfloat u;
        GLfloat v;
        GLfloat w;

        GlTextureArrayVertex() = default;
        GlTextureArrayVertex(const Vector3f& pos, const Vector3f& texCoord);
    };

    struct GlTexturedNormalVertex
    {
        GLfloat x;
        GLfloat y;
        GLfloat z;
        GLfloat u;
        GLfloat v;
        GLfloat nx;
        GLfloat ny;
        GLfloat nz;

        GlTexturedNormalVertex() = default;
        GlTexturedNormalVertex(const Vector3f& pos, const Vector2f& texCoord, const Vector3f& normal);
    };

    struct GlColoredVertex
    {
        GLfloat x;
        GLfloat y;
        GLfloat z;
        GLfloat r;
        GLfloat g;
        GLfloat b;

        GlColoredVertex() = default;
        GlColoredVertex(const Vector3f& pos, const Vector3f& color);
    };

    struct GlColoredNormalVertex
    {
        GLfloat x;
        GLfloat y;
        GLfloat z;
        GLfloat r;
        GLfloat g;
        GLfloat b;
        GLfloat nx;
        GLfloat ny;
        GLfloat nz;

        GlColoredNormalVertex() = default;
        GlColoredNormalVertex(const Vector3f& pos, const Vector3f& color, const Vector3f& normal);
    };
#pragma pack()

    struct AttribMapping
    {
        std::string name;
        GLuint location;

        AttribMapping(const std::string& name, GLuint location);
    };

    class GraphicsException : public std::runtime_error
    {
    public:
        explicit GraphicsException(const std::string& __arg);

        explicit GraphicsException(const char* string);
    };

    class OpenGlException : public GraphicsException
    {
    public:
        explicit OpenGlException(GLenum error);
    };

    struct FrameBufferInfo
    {
        TextureHandle texture;
        RenderBufferHandle depthBuffer;
        FrameBufferHandle frameBuffer;
    };

    class GraphicsContext
    {
    public:
        void clear();
        void clearColor();

        TextureHandle createTexture(const Grid<Color>& image);

        TextureHandle createTexture(unsigned int width, unsigned int height, const std::vector<Color>& image);

        TextureHandle createTexture(unsigned int width, unsigned int height, const Color* image);
        TextureHandle createEmptyTexture(unsigned int width, unsigned int height);

        TextureHandle createColorTexture(Color c);

        /**
         * One byte per texel, read back in the red channel. Nearest filtered
         * and without mipmaps, for images whose values are labels rather than
         * colours and must survive sampling unblended.
         */
        TextureHandle createSingleChannelTexture(unsigned int width, unsigned int height, const unsigned char* image);

        /**
         * The same, with the mip chain supplied rather than generated. Level 0
         * is first and each level is half the size of the one before it.
         */
        TextureHandle createSingleChannelMipMappedTexture(const std::vector<Grid<unsigned char>>& mipLevels);

        /**
         * Replaces a rectangle of a single-channel texture. The image pointer
         * is the whole image, of which the rectangle at (x, y) is uploaded.
         */
        void updateSingleChannelTexture(TextureIdentifier texture, unsigned int imageWidth, unsigned int x, unsigned int y, unsigned int width, unsigned int height, const unsigned char* image);

        /** Upload over an existing RGBA texture rather than making another one. */
        void updateTexture(TextureIdentifier texture, unsigned int width, unsigned int height, const Color* image);

        TextureArrayHandle createTextureArray(unsigned int width, unsigned int height, unsigned int mipMapLevels, std::vector<Color>& images);

        void enableDepthBuffer();

        void disableDepthBuffer();

        void enableDepthWrites();

        void disableDepthWrites();

        void enableDepthTest();

        void disableDepthTest();

        /**
         * Lets through only fragments that land exactly on the depth already
         * written, so a second pass over the same geometry touches each pixel
         * once. Restore with enableDepthTest.
         */
        void useDepthTestEqual();

        void enableCulling();


        ShaderHandle compileVertexShader(const std::string& source);

        ShaderHandle compileFragmentShader(const std::string& source);

        ShaderProgramHandle linkShaderProgram(ShaderIdentifier vertexShader, ShaderIdentifier fragmentShader, const std::vector<AttribMapping>& attribs);

        void enableColorBuffer();
        void disableColorBuffer();
        void enableStencilBuffer();
        void useStencilBufferForWrites();

        /** Subsequent draws reset the stencil to 0 where they land. */
        void useStencilBufferForClears();
        void useStencilBufferAsMask();
        void clearStencilBuffer();
        void disableStencilBuffer();

        GlMesh createTexturedMesh(const std::vector<GlTexturedVertex>& vertices, GLenum usage);

        GlMesh createTextureArrayMesh(const std::vector<GlTextureArrayVertex>& vertices, GLenum usage);

        GlMesh createColoredMesh(const std::vector<GlColoredVertex>& vertices, GLenum usage);

        GlMesh createTexturedNormalMesh(const std::vector<GlTexturedNormalVertex>& vertices, GLenum usage);

        GlMesh createColoredNormalMesh(const std::vector<GlColoredNormalVertex>& vertices, GLenum usage);

        void bindShader(ShaderProgramIdentifier shader);

        void unbindShader();

        void bindTexture(TextureIdentifier texture);

        void bindTextureArray(TextureArrayIdentifier texture);

        void unbindTexture();

        void unbindTextureArray();

        void bindFrameBuffer(FrameBufferIdentifier frameBuffer);

        /**
         * The framebuffer that unbindFrameBuffer returns to: the window's
         * own by default, or a presentation buffer while the frame is being
         * drawn at a scale (see SceneManager). Scenes never need to know
         * which, which is the point.
         */
        void setPresentationFrameBuffer(std::optional<FrameBufferIdentifier> frameBuffer);

        /**
         * Copies the colour of `source`, `sourceWidth` by `sourceHeight`
         * pixels, onto the window's own framebuffer stretched to
         * `windowWidth` by `windowHeight`, sampling nearest so a whole-number
         * scale keeps every pixel square. Leaves the window's framebuffer
         * bound.
         */
        void blitFrameBufferToWindow(FrameBufferIdentifier source, int sourceWidth, int sourceHeight, int windowWidth, int windowHeight);

        void unbindFrameBuffer();

        void enableBlending();

        void disableBlending();

        UniformLocation getUniformLocation(ShaderProgramIdentifier shader, const std::string& name);

        void setUniformInt(UniformLocation location, int value);
        void setUniformFloat(UniformLocation location, float value);
        void setUniformVec2(UniformLocation location, float a, float b);
        void setUniformVec3(UniformLocation location, float a, float b, float c);
        void setUniformVec4(UniformLocation location, float a, float b, float c, float d);
        void setUniformMatrix(UniformLocation location, const Matrix4f& matrix);
        void setUniformBool(UniformLocation location, bool value);

        void drawTriangles(const GlMesh& mesh);
        void drawLines(const GlMesh& mesh);
        void drawLineLoop(const GlMesh& mesh);

        Sprite createSprite(const Rectangle2f& bounds, const Rectangle2f& textureRegion, const SharedTextureHandle& texture);

        GlMesh createUnitTexturedQuad(const Rectangle2f& textureRegion);

        GlMesh createUnitTexturedQuadFlipped(const Rectangle2f& textureRegion);

        void setViewport(int x, int y, int width, int height);

        FrameBufferInfo createFrameBuffer(int width, int height);

        void bindFrameBufferColorBuffer(TextureIdentifier texture);

        /**
         * Attaches the building halo's coverage mask as a second render
         * target, so the passes that draw the world fill it as they go rather
         * than a second pass re-deriving it. Also disables blending for that
         * attachment alone, permanently. See worldPost.frag.
         */
        void attachFrameBufferMaskBuffer(FrameBufferIdentifier frameBuffer, TextureIdentifier texture);

        /**
         * Which of those two targets subsequent draws write. Passes that draw
         * solid world geometry use both; everything else uses one, so a
         * particle or a flash cannot punch a hole in the coverage.
         */
        void useSingleDrawBuffer();
        void useDualDrawBuffers();

        void setActiveTextureSlot0();
        void setActiveTextureSlot1();
        void setActiveTextureSlot2();
        void setActiveTextureSlot3();

    private:
        unsigned int presentationFrameBuffer{0};
        ShaderHandle compileShader(GLenum shaderType, const std::string& source);

        VboHandle genBuffer();
        VaoHandle genVertexArray();
        void bindBuffer(GLenum type, VboIdentifier id);
        void unbindBuffer(GLenum type);
        void bindVertexArray(VaoIdentifier id);
        void unbindVertexArray();
        void drawMesh(
            GLenum mode,
            const GlMesh& mesh,
            const Matrix4f& mvpMatrix,
            ShaderProgramIdentifier shader);
        void drawUnitMesh(
            const GlMesh& mesh,
            const Matrix4f& modelMatrix,
            const Matrix4f& mvpMatrix,
            float seaLevel,
            ShaderProgramIdentifier shader);
    };
}
