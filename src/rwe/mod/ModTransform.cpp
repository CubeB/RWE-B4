#include "ModTransform.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <rwe/util/Sha256.h>
#include <stdexcept>
#include <string>
#include <utility>

// Lua is built as C++ (MOD-SDK-023), so its headers are included as C++.
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

using json = nlohmann::json;

namespace rwe
{
    namespace
    {
        constexpr int hookInstructionCount = 1000;
        // The engine holds these in unsigned int; the transform surface
        // refuses a result it could not store rather than wrapping.
        constexpr double maxTransformValue = 4294967295.0;

        struct LuaMemory
        {
            std::size_t used = 0;
            std::size_t limit = 0;
        };

        void* limitedAllocator(void* userData, void* pointer, std::size_t oldSize, std::size_t newSize)
        {
            auto* memory = static_cast<LuaMemory*>(userData);
            // Lua passes a type tag rather than a size when pointer is null.
            const std::size_t previous = pointer ? oldSize : 0;
            if (newSize == 0)
            {
                if (pointer)
                {
                    memory->used -= previous;
                    std::free(pointer);
                }
                return nullptr;
            }
            if (newSize > previous && memory->used + (newSize - previous) > memory->limit)
            {
                return nullptr;
            }
            void* next = std::realloc(pointer, newSize);
            if (!next)
            {
                return nullptr;
            }
            memory->used = memory->used - previous + newSize;
            return next;
        }

        struct TransformHost
        {
            std::vector<ModUnitRecord>* units = nullptr;
            std::map<std::string, std::size_t> index;
            // The value each key held before this script touched it.
            std::map<std::pair<std::size_t, std::string>, double> originals;
            std::vector<std::string> log;
            ModTransformLimits limits;
        };

        struct SandboxState
        {
            TransformHost* host = nullptr;
            std::uint64_t budget = 0;
            std::uint64_t remaining = 0;
        };

        SandboxState* sandbox(lua_State* state)
        {
            return *static_cast<SandboxState**>(lua_getextraspace(state));
        }

        int luaUnitNames(lua_State* state)
        {
            const auto* host = sandbox(state)->host;
            lua_createtable(state, static_cast<int>(host->units->size()), 0);
            int index = 1;
            for (const auto& unit : *host->units)
            {
                lua_pushlstring(state, unit.name.data(), unit.name.size());
                lua_rawseti(state, -2, index++);
            }
            return 1;
        }

        int luaUnitGet(lua_State* state)
        {
            const auto* host = sandbox(state)->host;
            const std::string name = luaL_checkstring(state, 1);
            const std::string key = luaL_checkstring(state, 2);
            const auto found = host->index.find(name);
            if (found == host->index.end())
            {
                return luaL_error(state, "unknown unit \"%s\"", name.c_str());
            }
            const auto& values = (*host->units)[found->second].values;
            const auto value = values.find(key);
            if (value == values.end())
            {
                return luaL_error(state, "unit \"%s\" has no transformable key \"%s\"", name.c_str(), key.c_str());
            }
            lua_pushnumber(state, value->second);
            return 1;
        }

        int luaUnitSet(lua_State* state)
        {
            auto* host = sandbox(state)->host;
            const std::string name = luaL_checkstring(state, 1);
            const std::string key = luaL_checkstring(state, 2);
            const auto requested = luaL_checknumber(state, 3);
            const auto found = host->index.find(name);
            if (found == host->index.end())
            {
                return luaL_error(state, "unknown unit \"%s\"", name.c_str());
            }
            auto& values = (*host->units)[found->second].values;
            const auto value = values.find(key);
            if (value == values.end())
            {
                return luaL_error(state, "unit \"%s\" has no transformable key \"%s\"", name.c_str(), key.c_str());
            }
            if (!std::isfinite(requested) || requested < 0.0 || requested > maxTransformValue)
            {
                return luaL_error(state, "unit \"%s\" key \"%s\" must be a finite value in [0, 4294967295]",
                    name.c_str(), key.c_str());
            }
            const auto original = host->originals.find({found->second, key});
            if (original == host->originals.end())
            {
                if (host->originals.size() >= host->limits.maxChanges)
                {
                    return luaL_error(state, "more than %s unit values were changed",
                        std::to_string(host->limits.maxChanges).c_str());
                }
                host->originals.emplace(std::make_pair(found->second, key), value->second);
            }
            value->second = requested;
            return 0;
        }

        int luaLog(lua_State* state)
        {
            auto* host = sandbox(state)->host;
            std::size_t length = 0;
            const char* text = luaL_checklstring(state, 1, &length);
            std::string message(text, length);
            if (message.size() > host->limits.maxLogLength)
            {
                message.resize(host->limits.maxLogLength);
            }
            if (host->log.size() < host->limits.maxLogEntries)
            {
                host->log.push_back(std::move(message));
            }
            return 0;
        }

        void instructionHook(lua_State* state, lua_Debug*)
        {
            auto* current = sandbox(state);
            if (current->remaining <= hookInstructionCount)
            {
                current->remaining = 0;
                luaL_error(state, "instruction budget exhausted");
            }
            current->remaining -= hookInstructionCount;
        }

        void pushJsonValue(lua_State* state, const json& value)
        {
            switch (value.type())
            {
            case json::value_t::null:
                lua_pushnil(state);
                break;
            case json::value_t::boolean:
                lua_pushboolean(state, value.get<bool>());
                break;
            case json::value_t::number_integer:
            case json::value_t::number_unsigned:
            case json::value_t::number_float:
                lua_pushnumber(state, value.get<double>());
                break;
            case json::value_t::string:
            {
                const auto& text = value.get_ref<const std::string&>();
                lua_pushlstring(state, text.data(), text.size());
                break;
            }
            default:
                // Manifest options are scalars; a container is not reachable
                // from a valid manifest, but never hand Lua something partial.
                lua_pushnil(state);
                break;
            }
        }

        std::string takeError(lua_State* state)
        {
            const char* message = lua_tostring(state, -1);
            std::string text = message ? message : "unknown Lua error";
            lua_pop(state, 1);
            return text;
        }

        void openSandboxLibraries(lua_State* state)
        {
            const luaL_Reg libraries[] = {
                {"_G", luaopen_base},
                {LUA_TABLIBNAME, luaopen_table},
                {LUA_STRLIBNAME, luaopen_string},
                {LUA_MATHLIBNAME, luaopen_math},
                {LUA_UTF8LIBNAME, luaopen_utf8},
                {LUA_COLIBNAME, luaopen_coroutine},
            };
            for (const auto& library : libraries)
            {
                luaL_requiref(state, library.name, library.func, 1);
                lua_pop(state, 1);
            }

            // No code loading, no filesystem, no process, no clock.
            for (const auto* name : {"dofile", "loadfile", "load"})
            {
                lua_pushnil(state);
                lua_setglobal(state, name);
            }

            // math.random is not part of the deterministic surface. A mod
            // that needs randomness gets it from the engine's seeded stream
            // in a later revision, not from the C library.
            lua_getglobal(state, "math");
            for (const auto* name : {"random", "randomseed"})
            {
                lua_pushnil(state);
                lua_setfield(state, -2, name);
            }
            lua_pop(state, 1);

            // print goes to the transform log rather than a stdout stream the
            // engine does not own.
            lua_pushcfunction(state, luaLog);
            lua_setglobal(state, "print");
        }

        void buildRweTable(lua_State* state, const ModTransformScript& script)
        {
            lua_newtable(state);
            lua_pushinteger(state, currentModTransformApiVersion);
            lua_setfield(state, -2, "api_version");
            lua_pushcfunction(state, luaUnitNames);
            lua_setfield(state, -2, "unit_names");
            lua_pushcfunction(state, luaUnitGet);
            lua_setfield(state, -2, "unit_get");
            lua_pushcfunction(state, luaUnitSet);
            lua_setfield(state, -2, "unit_set");
            lua_pushcfunction(state, luaLog);
            lua_setfield(state, -2, "log");

            lua_newtable(state);
            if (script.options.is_object())
            {
                for (const auto& [key, value] : script.options.items())
                {
                    pushJsonValue(state, value);
                    lua_setfield(state, -2, key.c_str());
                }
            }
            lua_setfield(state, -2, "options");
            lua_setglobal(state, "rwe");
        }
    }

    std::string describeModSetMismatch(const ModLaunchContent* expected, const ModLaunchContent* recorded)
    {
        const std::string expectedIdentity = expected ? expected->identity : std::string();
        const std::string recordedIdentity = recorded ? recorded->identity : std::string();
        if (expectedIdentity == recordedIdentity)
        {
            return {};
        }
        if (recordedIdentity.empty())
        {
            return "the game was recorded without mods but this launch has a mod set ("
                + expectedIdentity + ")";
        }
        if (expectedIdentity.empty())
        {
            return "the game was recorded with mod set " + recordedIdentity + " but this launch has no mods";
        }
        if (expected && recorded && expected->packages.size() == recorded->packages.size())
        {
            for (std::size_t i = 0; i < expected->packages.size(); ++i)
            {
                const auto& a = expected->packages[i];
                const auto& b = recorded->packages[i];
                if (a.id != b.id)
                {
                    return "mod order differs at position " + std::to_string(i + 1) + ": this launch has \""
                        + a.id + "\", the recording has \"" + b.id + "\"";
                }
                if (a.version != b.version || a.manifestSha256 != b.manifestSha256)
                {
                    return "mod \"" + a.id + "\" differs: this launch has version " + a.version + " ("
                        + a.manifestSha256 + "), the recording has " + b.version + " (" + b.manifestSha256 + ")";
                }
                if (a.entrySha256 != b.entrySha256)
                {
                    return "mod \"" + a.id + "\" " + a.version + " has a different script: this launch has "
                        + (a.entrySha256.empty() ? std::string("none") : a.entrySha256) + ", the recording has "
                        + (b.entrySha256.empty() ? std::string("none") : b.entrySha256);
                }
            }
        }
        return "the recorded mod set " + recordedIdentity + " is not this launch's set " + expectedIdentity;
    }

    json modLaunchContentToJson(const ModLaunchContent* content)
    {
        if (!content || content->identity.empty())
        {
            return nullptr;
        }
        json packages = json::array();
        for (const auto& package : content->packages)
        {
            packages.push_back({
                {"id", package.id},
                {"version", package.version},
                {"manifestSha256", package.manifestSha256},
                {"entrySha256", package.entrySha256},
                {"scriptName", package.scriptName},
                {"kind", package.kind},
                {"features", package.features},
                {"resourceDirectories", package.resourceDirectories},
            });
        }
        return json{{"identity", content->identity}, {"packages", std::move(packages)}};
    }

    std::shared_ptr<const ModLaunchContent> modLaunchContentFromJson(const json& value)
    {
        if (value.is_null())
        {
            return nullptr;
        }
        if (!value.is_object() || !value.contains("identity") || !value["identity"].is_string())
        {
            throw std::runtime_error("a recorded mod set must carry a string identity");
        }
        auto content = std::make_shared<ModLaunchContent>();
        content->identity = value["identity"].get<std::string>();
        if (content->identity.size() != 64
            || content->identity.find_first_not_of("0123456789abcdef") != std::string::npos)
        {
            throw std::runtime_error("a recorded mod set identity must be 64 lowercase hex characters");
        }
        if (value.contains("packages"))
        {
            if (!value["packages"].is_array())
            {
                throw std::runtime_error("a recorded mod set's packages must be an array");
            }
            for (const auto& item : value["packages"])
            {
                if (!item.is_object())
                {
                    throw std::runtime_error("a recorded mod package must be an object");
                }
                ModLaunchPackage package;
                package.id = item.value("id", std::string());
                package.version = item.value("version", std::string());
                package.manifestSha256 = item.value("manifestSha256", std::string());
                // Absent in a record written before MOD-SDK-014; such a record
                // also carries a format-1 identity, which no current launch
                // reproduces, so it is refused by identity, not by this field.
                package.entrySha256 = item.value("entrySha256", std::string());
                package.scriptName = item.value("scriptName", std::string());
                package.kind = item.value("kind", std::string());
                if (item.contains("features"))
                {
                    package.features = item.value("features", std::vector<std::string>());
                }
                if (item.contains("resourceDirectories"))
                {
                    package.resourceDirectories = item.value("resourceDirectories",
                        std::map<std::string, std::string>());
                }
                content->packages.push_back(std::move(package));
            }
        }
        return content;
    }

    std::string readVerifiedModScript(const ModLaunchPackage& package)
    {
        constexpr std::size_t maxScriptBytes = 1024 * 1024;
        if (package.entrySha256.empty())
        {
            throw std::runtime_error("Mod " + package.id
                + ": the resolved set carries no script hash; resolve the set again with this build");
        }
        std::ifstream scriptFile(package.scriptPath, std::ios::binary);
        if (!scriptFile)
        {
            throw std::runtime_error("Mod " + package.id + ": cannot open " + package.scriptPath);
        }
        std::string source((std::istreambuf_iterator<char>(scriptFile)), std::istreambuf_iterator<char>());
        if (source.size() > maxScriptBytes)
        {
            throw std::runtime_error("Mod " + package.id + ": script is larger than 1 MiB");
        }
        const auto actual = sha256Hex(source);
        if (actual != package.entrySha256)
        {
            throw std::runtime_error("Mod " + package.id + ": " + package.scriptName
                + " changed after the mod set was resolved (expected " + package.entrySha256 + ", found "
                + actual + "); resolve the set again");
        }
        return source;
    }

    bool isSupportedTransformKey(const std::string& key)
    {
        return key == "MetalCost" || key == "EnergyCost" || key == "BuildTime";
    }

    ModTransformResult runUnitTransform(
        const ModTransformScript& script,
        std::vector<ModUnitRecord>& units,
        const ModTransformLimits& limits)
    {
        ModTransformResult result;

        // Stable, name-ordered iteration is part of the contract: every peer
        // sees unit_names() in the same order regardless of archive layout.
        std::sort(units.begin(), units.end(), [](const ModUnitRecord& a, const ModUnitRecord& b) {
            return a.name < b.name;
        });

        TransformHost host;
        host.units = &units;
        host.limits = limits;
        for (std::size_t i = 0; i < units.size(); ++i)
        {
            if (!host.index.emplace(units[i].name, i).second)
            {
                result.error = "unit \"" + units[i].name + "\" appears twice in the transform input";
                return result;
            }
            for (const auto& [key, value] : units[i].values)
            {
                if (!isSupportedTransformKey(key))
                {
                    result.error = "transform input for unit \"" + units[i].name + "\" carries unsupported key \""
                        + key + "\"";
                    return result;
                }
                (void)value;
            }
        }

        LuaMemory memory;
        memory.limit = limits.memoryBudget;
        lua_State* state = lua_newstate(limitedAllocator, &memory);
        if (!state)
        {
            result.error = "cannot create a Lua state within the " + std::to_string(limits.memoryBudget)
                + "-byte memory budget";
            return result;
        }

        SandboxState sandboxState;
        sandboxState.host = &host;
        sandboxState.budget = limits.instructionBudget;
        sandboxState.remaining = limits.instructionBudget;
        *static_cast<SandboxState**>(lua_getextraspace(state)) = &sandboxState;

        openSandboxLibraries(state);
        buildRweTable(state, script);
        lua_sethook(state, instructionHook, LUA_MASKCOUNT, hookInstructionCount);

        const int loadStatus = luaL_loadbufferx(state, script.source.data(), script.source.size(),
            script.scriptName.c_str(), "t");
        if (loadStatus != LUA_OK)
        {
            result.error = takeError(state);
        }
        else if (lua_pcall(state, 0, 0, 0) != LUA_OK)
        {
            result.error = takeError(state);
        }

        result.instructionsUsed = sandboxState.budget - sandboxState.remaining;
        result.log = host.log;

        if (result.error.empty())
        {
            // Commit: round once, half away from zero, and journal only what
            // actually moved.
            for (const auto& [key, original] : host.originals)
            {
                auto& unit = units[key.first];
                auto& value = unit.values.at(key.second);
                const auto rounded = std::floor(value + 0.5);
                if (rounded < 0.0 || rounded > maxTransformValue)
                {
                    result.error = "unit \"" + unit.name + "\" key \"" + key.second + "\" rounds to an unstorable value";
                    break;
                }
                value = rounded;
                if (rounded != original)
                {
                    result.changes.push_back({unit.name, key.second, original, rounded});
                }
            }
        }

        if (!result.error.empty())
        {
            // A failed script leaves the input exactly as it arrived.
            for (const auto& [key, original] : host.originals)
            {
                units[key.first].values[key.second] = original;
            }
            result.changes.clear();
        }

        lua_close(state);
        if (memory.used != 0)
        {
            // Lua frees everything on close; a non-zero count would mean the
            // allocator bookkeeping is wrong, which is worth surfacing.
            result.error = result.error.empty()
                ? "Lua state leaked " + std::to_string(memory.used) + " bytes"
                : result.error;
            result.ok = false;
            return result;
        }

        result.ok = result.error.empty();
        return result;
    }
}
