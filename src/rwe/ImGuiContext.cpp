#include "ImGuiContext.h"
#include <SDL3/SDL_events.h>

namespace rwe
{
    ImGuiContext::~ImGuiContext()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    bool isKeyboardEvent(const SDL_Event& event)
    {
        return event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_TEXT_INPUT;
    }

    bool isMouseEvent(const SDL_Event& event)
    {
        return event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL;
    }

    bool wantsEvent(const ImGuiIO& io, const SDL_Event& event)
    {
        // Keys are only kept from the game while a text field is being typed
        // into. Merely having a debug window focused must not swallow Pause,
        // Escape or the F-keys, or the player cannot get back to the game.
        if (io.WantTextInput && isKeyboardEvent(event))
        {
            return true;
        }

        if (io.WantCaptureMouse && isMouseEvent(event))
        {
            return true;
        }

        return false;
    }

    bool ImGuiContext::processEvent(const SDL_Event& event)
    {
        // ImGui always gets to see keyboard and mouse events so its own state
        // (hover, focus, key chords) stays right; the return value only says
        // whether the game should ignore the event.
        if (isKeyboardEvent(event) || isMouseEvent(event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }
        return wantsEvent(*io, event);
    }

    ImGuiContext::ImGuiContext(const std::string& iniPath, SDL_Window* window, void* glContext) : iniPath(iniPath)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        io = &ImGui::GetIO();
        io->IniFilename = this->iniPath.data();
        io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        ImGui_ImplSDL3_InitForOpenGL(window, glContext);
        ImGui_ImplOpenGL3_Init("#version 150");
    }

    void ImGuiContext::newFrame(SDL_Window* window)
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiContext::render()
    {
        ImGui::Render();
    }

    void ImGuiContext::renderDrawData()
    {
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}
