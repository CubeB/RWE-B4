#pragma once

#include <optional>
#include <rwe/mod/ModStateStore.h>

struct lua_State;

namespace rwe
{
    // Conversions between Lua values and ModValue, shared by the runtime
    // (ModRuntime) and scripted AI (ModAiController) so both store the same
    // value for the same Lua argument. MOD-SDK-016.
    //
    // Lua is built as C, and luaL_error longjmps past C++ destructors. So a
    // host function classifies its argument with modLuaValueKind first,
    // raises any error while no C++ object is alive, and only then builds
    // the ModValue with modLuaValueAt.
    enum class ModLuaValueKind
    {
        Invalid,
        NonFinite,
        Boolean,
        Number,
        Text,
    };

    // A boolean, a finite number, or a string. A numeric string stays a
    // string: Lua's own number coercion is not applied.
    ModLuaValueKind modLuaValueKind(lua_State* state, int index);

    // The value at `index`, of a kind modLuaValueKind accepted. A boolean is
    // stored as i32 0 or 1, a number as f32, a string as text.
    ModValue modLuaValueAt(lua_State* state, int index, ModLuaValueKind kind);

    // Pushes the value, or nil when there is none.
    void pushModLuaValue(lua_State* state, const std::optional<ModValue>& value);
}
