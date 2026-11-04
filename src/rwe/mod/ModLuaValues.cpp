#include "ModLuaValues.h"

#include <cmath>
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
            return std::isfinite(static_cast<float>(lua_tonumber(state, index))) ? ModLuaValueKind::Number
                                                                                   : ModLuaValueKind::NonFinite;
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
