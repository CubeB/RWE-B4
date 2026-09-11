#pragma once

#include <array>
#include <memory>
#include <rwe/UiRenderService.h>
#include <rwe/render/GraphicsContext.h>
#include <rwe/render/SpriteSeries.h>
#include <rwe/rwe_time.h>
#include <rwe/sdl/SdlContext.h>

namespace rwe
{
    enum class CursorType : size_t
    {
        Normal,
        Select,
        Attack,
        Move,
        Guard,
        Repair,
        Reclaim,
        Patrol,
        Capture,
        Load,
        /**
         * The hook an air transport shows over a unit it could lift. The
         * original splits the load cursor on `canfly` and on nothing else
         * (0x43E7F1): an aircraft gets `cursorpickup`, a crane, ship or
         * hovercraft gets `cursorload`. See TOTALA-EXE.md S:103.
         */
        Pickup,
        Unload,
        Red,
        Green,
        /**
         * Not a cursor: the star the original strings along the line between
         * queued waypoints. It lives in the same GAF and is loaded the same
         * way, so it is simplest to keep it here.
         */
        PathIcon,
        NUM_CURSORS
    };

    using Cursors = std::array<std::shared_ptr<SpriteSeries>, static_cast<size_t>(CursorType::NUM_CURSORS)>;

    size_t operator*(CursorType t);

    class CursorService
    {
    private:
        SdlContext* sdlContext;
        TimeService* timeService;

        const Cursors _cursors;

        SpriteSeries* currentCursor;

    public:
        CursorService(SdlContext* sdlContext, TimeService* timeService, Cursors cursors);

        void useCursor(CursorType type);

        std::shared_ptr<SpriteSeries> getCursor(CursorType type) const;

        void render(UiRenderService& renderer) const;
    };
}
