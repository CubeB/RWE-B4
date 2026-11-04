#include "ModLuaValues.h"

#include <limits>
#include <string>

extern "C"
{
#include <lua.h>
}

namespace rwe
{
    ModLuaValueKind modLuaValueKind(lua_State* state, int index)
    {
        if (lua_isboolean(state, index))
        {
            return ModLuaValueKind::Boolean;
        }
        if (lua_type(state, index) == LUA_TNUMBER)
        {
            // Decided on the Lua double, by comparison alone: NaN is the one
            // value unequal to itself, and anything beyond FLT_MAX would
            // become an infinity as an f32. No library classification is
            // relied on, so no floating-point mode can let a NaN through
            // (MSVC Release, MOD-SDK-016).
            const lua_Number number = lua_tonumber(state, index);
            const auto largest = static_cast<lua_Number>(std::numeric_limits<float>::max());
            const bool finite = number == number && number <= largest && number >= -largest;
            return finite ? ModLuaValueKind::Number : ModLuaValueKind::NonFinite;
        }
        if (lua_type(state, index) == LUA_TSTRING)
        {
            return ModLuaValueKind::Text;
        }
        return ModLuaValueKind::Invalid;
    }

    ModValue modLuaValueAt(lua_State* state, int index, ModLuaValueKind kind)
    {
        switch (kind)
        {
        case ModLuaValueKind::Boolean:
            return ModValue::integer(lua_toboolean(state, index) ? 1 : 0);
        case ModLuaValueKind::Number:
            return ModValue::number(static_cast<float>(lua_tonumber(state, index)));
        default:
        {
            std::size_t length = 0;
            const char* text = lua_tolstring(state, index, &length);
            return ModValue::text(std::string(text, length));
        }
        }
    }

    void pushModLuaValue(lua_State* state, const std::optional<ModValue>& value)
    {
        if (!value)
        {
            lua_pushnil(state);
            return;
        }
        switch (value->type)
        {
        case ModValueType::I32:
            lua_pushinteger(state, value->i32);
            break;
        case ModValueType::F32:
            lua_pushnumber(state, value->f32);
            break;
        case ModValueType::String:
        case ModValueType::Bytes:
            lua_pushlstring(state, value->bytes.data(), value->bytes.size());
            break;
        }
    }
}
