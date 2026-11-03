#pragma once

#include <rwe/ColorPalette.h>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * MOD-10, docs/MODDING-SDK-FINDINGS.md MOD-SDK-013.
     *
     * What a presentation mod asked to draw. A plain value list rather than
     * direct calls into `UiRenderService`: the runtime can collect a frame's
     * commands without holding a renderer, and a failed mod can have its list
     * thrown away with nothing half-drawn.
     *
     * Coordinates are UI pixels, the same space the native chrome is laid out
     * in. Nothing here carries a texture or a model handle, so a mod cannot
     * reach the renderer's own resources.
     */
    struct PresentationDrawCommand
    {
        enum class Kind
        {
            /** A filled rectangle. */
            Rect,
            /** A line one pixel wide unless a thickness is given. */
            Line,
            /** A line of text in the game's chrome font. */
            Text,
            /** A horizontal bar filled to `fraction` of its width. */
            Bar,
        };

        Kind kind{Kind::Rect};

        float x{0.0f};
        float y{0.0f};
        float width{0.0f};
        float height{0.0f};

        /** Line ends, used only by Kind::Line. */
        float x2{0.0f};
        float y2{0.0f};
        float thickness{1.0f};

        /** Bar fill, 0 to 1, clamped before drawing. */
        float fraction{0.0f};

        Color color{Color(255, 255, 255, 255)};

        /** Text, used only by Kind::Text. */
        std::string text;
    };

    struct PresentationDrawList
    {
        std::vector<PresentationDrawCommand> commands;

        void clear() { commands.clear(); }

        bool empty() const { return commands.empty(); }

        void add(PresentationDrawCommand command) { commands.push_back(std::move(command)); }
    };
}
