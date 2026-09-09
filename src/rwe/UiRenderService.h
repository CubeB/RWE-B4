#pragma once

#include <rwe/ShaderService.h>
#include <algorithm>
#include <rwe/Viewport.h>
#include <rwe/render/GraphicsContext.h>
#include <stack>

namespace rwe
{
    struct UiOrthoBounds
    {
        float left;
        float right;
        float bottom;
        float top;
    };

    /**
     * The orthographic box a fixed-size UI is drawn through, widened on
     * whichever axis has room to spare so that the UI keeps its proportions
     * in a window that is not its shape.
     *
     * A window at the content's own aspect gets the content's own box back,
     * so this is identity for everything that was already the right shape.
     * A window of zero height (minimised, on Windows) gets the same, rather
     * than a division by zero.
     */
    UiOrthoBounds computeUiOrthoBounds(float contentWidth, float contentHeight, float windowWidth, float windowHeight);

    class UiRenderService
    {
    private:
        GraphicsContext* graphics;
        ShaderService* shaders;
        const AbstractViewport* viewport;

        /**
         * The window the fixed-size UI above lands in, when it is not the
         * same shape as that UI.
         *
         * The menus are laid out at 640x480 because that is what the original
         * laid them out at and what its GUI files carry, and the projection
         * used to map that box onto the whole window whatever shape the
         * window was -- so a 16:9 display stretched every button, dial and
         * piece of art by a third. With this set, the projection is widened
         * on whichever axis has room to spare instead, so the 640x480 keeps
         * its proportions and sits in the middle with the slack showing as
         * bars. MovieScene has done the same for its films since they landed;
         * this is that, for the menus.
         *
         * Null for a UI already drawn at the window's own size, where there
         * is nothing to reconcile -- the in-game HUD, which is native by
         * design so that a bigger window means more world and not a bigger
         * interface.
         */
        const AbstractViewport* aspectViewport{nullptr};

        std::stack<Matrix4f> matrixStack{{Matrix4f::identity()}};

    public:
        UiOrthoBounds getOrthoBounds() const;

    public:
        UiRenderService(GraphicsContext* graphics, ShaderService* shaders, const AbstractViewport* viewport);
        UiRenderService(GraphicsContext* graphics, ShaderService* shaders, const AbstractViewport* viewport, const AbstractViewport* aspectViewport);

        void fillScreen(const Color& color);

        void drawSprite(float x, float y, const Sprite& sprite);

        void drawSprite(float x, float y, const Sprite& sprite, const Color& tint);

        /** Draws the sprite, ignoring its internal x and y offset. */
        void drawSpriteAbs(float x, float y, const Sprite& sprite);

        void drawSpriteAbs(float x, float y, float width, float height, const Sprite& sprite);

        void drawSpriteAbs(const Rectangle2f& rect, const Sprite& sprite);

        void drawText(float x, float y, const std::string& text, const SpriteSeries& font);
        void drawText(float x, float y, const std::string& text, const SpriteSeries& font, const Color& tint);

        void drawTextWrapped(Rectangle2f area, const std::string& text, const SpriteSeries& font);

        void drawTextCentered(float x, float y, const std::string& text, const SpriteSeries& font);

        void drawTextCenteredX(float x, float y, const std::string& text, const SpriteSeries& font);

        void drawTextAlignRight(float x, float y, const std::string& text, const SpriteSeries& font);
        void drawTextAlignRight(float x, float y, const std::string& text, const SpriteSeries& font, const Color& tint);

        float getTextWidth(const std::string& text, const SpriteSeries& font);

        void pushMatrix();

        void popMatrix();

        /**
         * Multiplies the current OpenGL matrix with the specified matrix.
         * If the current OpenGL matrix is C and the coordinates to be transformed are v,
         * meaning that the current transformation would be C * v,
         * then calling this with an argument M replaces the current transformation with (C * M) * v.
         * Intuitively this means that any transformation you pass in here
         * will be done directly on the coordinates,
         * and the existing tranformation will be done on the result of that.
         */
        void multiplyMatrix(const Matrix4f& matrix);

        template <typename It>
        float getTextWidth(It begin, It end, const SpriteSeries& font);

        template <typename It>
        It findEndOfWord(It it, It end);

        void fillColor(float x, float y, float width, float height, Color color);

        void drawHealthBar(float x, float y, float percentFull);

        void drawHealthBar2(float x, float y, float width, float height, float percentFull);

        void drawBoxOutline(float x, float y, float width, float height, Color color);

        void drawBoxOutline(float x, float y, float width, float height, Color color, float thickness);

        void drawLine(const Vector2f& start, const Vector2f& end);

        /** A closed one-pixel outline through the given points. */
        void drawLineLoop(const std::vector<Vector2f>& points, const Color& color);

        /**
         * Disjoint line segments, two points each, in one draw call. The
         * minimap rings need it: clipped to the minimap they are no longer
         * one closed loop, and a dashed ring is not one to start with.
         */
        void drawLines(const std::vector<Vector2f>& points, const Color& color);

        Matrix4f getViewProjectionMatrix() const;

        Matrix4f getInverseViewProjectionMatrix() const;
    };

    template <typename It>
    float UiRenderService::getTextWidth(It it, It end, const SpriteSeries& font)
    {
        float width = 0;
        for (; it != end; ++it)
        {
            auto ch = *it;

            if (ch > font.sprites.size())
            {
                ch = 0;
            }

            const auto& sprite = *font.sprites[ch];

            width += sprite.bounds.right();
        }

        return width;
    }

    template <typename It>
    It UiRenderService::findEndOfWord(It it, It end)
    {
        return std::find_if(it, end, [](int ch) { return ch == ' '; });
    }
}
