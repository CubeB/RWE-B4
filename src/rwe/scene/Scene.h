#pragma once

#include <SDL3/SDL.h>
#include <rwe/events.h>
#include <string>

namespace rwe
{
    class Scene
    {
    public:
        virtual void update(int millisecondsElapsed) {}

        virtual void init() {}

        virtual void render() {}

        virtual void onKeyDown(const SDL_KeyboardEvent& /*key*/) {}

        virtual void onKeyUp(const SDL_KeyboardEvent& /*key*/) {}

        /** Composed text from the keyboard, for whatever the scene has focused. */
        virtual void onTextInput(const std::string& /*text*/) {}

        virtual void onMouseDown(MouseButtonEvent /*event*/) {}

        virtual void onMouseUp(MouseButtonEvent /*event*/) {}

        virtual void onMouseMove(MouseMoveEvent /*event*/) {}

        virtual void onMouseWheel(MouseWheelEvent /*event*/) {}

        virtual ~Scene() = default;
    };
}
