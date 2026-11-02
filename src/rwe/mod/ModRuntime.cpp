#include "ModRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <rwe/util/Sha256.h>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using json = nlohmann::json;

namespace rwe
{
    namespace
    {
        constexpr int hookInstructionCount = 1000;

        struct LuaMemory
        {
            std::size_t used = 0;
            std::size_t limit = 0;
        };

        void* limitedAllocator(void* userData, void* pointer, std::size_t oldSize, std::size_t newSize)
        {
            auto* memory = static_cast<LuaMemory*>(userData);
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
    }

    class ModRuntime::Impl
    {
    public:
        struct Package
        {
            ModRuntimePackage spec;
            lua_State* state = nullptr;
            LuaMemory memory;
            std::uint64_t remaining = 0;
            std::minstd_rand rng;
            bool disabled = false;
        };

        struct Context
        {
            Impl* impl = nullptr;
            std::size_t index = 0;
        };

        Impl(std::vector<ModRuntimePackage> packages, std::uint32_t seed, ModStateStore& store, ModRuntimeLimits limits)
            : store(&store), limits(limits)
        {
            this->packages.reserve(packages.size());
            contexts.reserve(packages.size());
            for (auto& package : packages)
            {
                Package entry;
                entry.spec = std::move(package);
                entry.memory.limit = limits.memoryBudgetPerPackage;
                const auto digest = sha256Hex(entry.spec.id);
                const auto idSeed = static_cast<std::uint32_t>(std::stoul(digest.substr(0, 8), nullptr, 16));
                entry.rng.seed(seed ^ idSeed);
                this->packages.push_back(std::move(entry));
            }
            for (std::size_t i = 0; i < this->packages.size(); ++i)
            {
                auto context = std::make_unique<Context>();
                context->impl = this;
                context->index = i;
                contexts.push_back(std::move(context));
            }
            for (std::size_t i = 0; i < this->packages.size(); ++i)
            {
                openPackage(i);
            }
        }

        ~Impl()
        {
            for (auto& package : packages)
            {
                if (package.state)
                {
                    lua_close(package.state);
                }
            }
        }

        Package& packageOf(lua_State* state)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            return packages[context->index];
        }

        void disable(Package& package, const std::string& why)
        {
            if (package.disabled)
            {
                return;
            }
            package.disabled = true;
            addLog("disabling " + package.spec.id + ": " + why);
        }

        void addLog(std::string message)
        {
            if (message.size() > limits.maxLogLength)
            {
                message.resize(limits.maxLogLength);
            }
            if (logLines.size() < limits.maxLogEntries)
            {
                logLines.push_back(std::move(message));
            }
        }

        void openPackage(std::size_t index)
        {
            auto& package = packages[index];
            auto* state = lua_newstate(limitedAllocator, &package.memory);
            if (!state)
            {
                throw std::runtime_error("Mod " + package.spec.id + ": cannot create a Lua state within its memory budget");
            }
            package.state = state;
            *static_cast<Context**>(lua_getextraspace(state)) = contexts[index].get();

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
            for (const auto* name : {"dofile", "loadfile", "load"})
            {
                lua_pushnil(state);
                lua_setglobal(state, name);
            }
            lua_getglobal(state, "math");
            for (const auto* name : {"random", "randomseed"})
            {
                lua_pushnil(state);
                lua_setfield(state, -2, name);
            }
            lua_pop(state, 1);
            lua_pushcfunction(state, logFunction);
            lua_setglobal(state, "print");

            lua_newtable(state);
            lua_pushinteger(state, currentModRuntimeApiVersion);
            lua_setfield(state, -2, "api_version");
            lua_pushcfunction(state, logFunction);
            lua_setfield(state, -2, "log");
            lua_pushcfunction(state, storeGetFunction);
            lua_setfield(state, -2, "store_get");
            lua_pushcfunction(state, storeSetFunction);
            lua_setfield(state, -2, "store_set");
            lua_pushcfunction(state, randBelowFunction);
            lua_setfield(state, -2, "rand_below");
            lua_newtable(state);
            if (package.spec.options.is_object())
            {
                for (const auto& [key, value] : package.spec.options.items())
                {
                    pushJson(state, value);
                    lua_setfield(state, -2, key.c_str());
                }
            }
            lua_setfield(state, -2, "options");
            lua_setglobal(state, "rwe");

            // Load and register the package's hooks. A script that does not
            // load is disabled like one that faults, rather than leaving a
            // silently empty package in the fold.
            package.remaining = limits.instructionBudgetPerCall;
            lua_sethook(state, instructionHook, LUA_MASKCOUNT, hookInstructionCount);
            const int loadStatus = luaL_loadbufferx(state, package.spec.source.data(), package.spec.source.size(),
                package.spec.scriptName.c_str(), "t");
            if (loadStatus != LUA_OK)
            {
                const char* message = lua_tostring(state, -1);
                disable(package, message ? message : "script did not load");
                lua_pop(state, 1);
                return;
            }
            if (lua_pcall(state, 0, 0, 0) != LUA_OK)
            {
                const char* message = lua_tostring(state, -1);
                disable(package, message ? message : "script failed at load");
                lua_pop(state, 1);
            }
        }

        static void pushJson(lua_State* state, const json& value)
        {
            switch (value.type())
            {
            case json::value_t::boolean:
                lua_pushboolean(state, value.get<bool>());
                break;
            case json::value_t::number_integer:
            case json::value_t::number_unsigned:
            case json::value_t::number_float:
                lua_pushnumber(state, value.get<double>());
                break;
            case json::value_t::string:
                lua_pushlstring(state, value.get_ref<const std::string&>().data(), value.get_ref<const std::string&>().size());
                break;
            default:
                lua_pushnil(state);
                break;
            }
        }

        static void instructionHook(lua_State* state, lua_Debug*)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            auto& package = context->impl->packages[context->index];
            if (package.remaining <= hookInstructionCount)
            {
                package.remaining = 0;
                luaL_error(state, "instruction budget exhausted");
            }
            package.remaining -= hookInstructionCount;
        }

        static int logFunction(lua_State* state)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            std::size_t length = 0;
            const char* text = luaL_checklstring(state, 1, &length);
            context->impl->addLog(std::string(text, length));
            return 0;
        }

        static int storeGetFunction(lua_State* state)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            auto* impl = context->impl;
            const auto& package = impl->packages[context->index];
            const std::string key = luaL_checkstring(state, 1);
            const auto value = impl->store->getGlobal(package.spec.id, key);
            if (!value)
            {
                lua_pushnil(state);
                return 1;
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
            return 1;
        }

        static int storeSetFunction(lua_State* state)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            auto* impl = context->impl;
            const auto& package = impl->packages[context->index];
            const std::string key = luaL_checkstring(state, 1);
            std::optional<ModValue> value;
            if (lua_isboolean(state, 2))
            {
                value = ModValue::integer(lua_toboolean(state, 2) ? 1 : 0);
            }
            else if (lua_isnumber(state, 2))
            {
                const auto number = static_cast<float>(lua_tonumber(state, 2));
                if (!std::isfinite(number))
                {
                    return luaL_error(state, "store_set value must be finite");
                }
                value = ModValue::number(number);
            }
            else if (lua_isstring(state, 2))
            {
                std::size_t length = 0;
                const char* text = lua_tolstring(state, 2, &length);
                value = ModValue::text(std::string(text, length));
            }
            else
            {
                return luaL_error(state, "store_set value must be a number, string or boolean");
            }
            if (!impl->store->setGlobal(package.spec.id, key, *value))
            {
                return luaL_error(state, "store quota exceeded for %s", package.spec.id.c_str());
            }
            return 0;
        }

        static int randBelowFunction(lua_State* state)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            auto& package = context->impl->packages[context->index];
            const auto bound = luaL_checkinteger(state, 1);
            if (bound <= 0)
            {
                return luaL_error(state, "rand_below needs a positive bound");
            }
            // Deterministic modulo, as SimRandom.h's randomBelow does.
            lua_pushinteger(state, static_cast<lua_Integer>(package.rng() % static_cast<unsigned int>(bound)));
            return 1;
        }

        bool callDamage(std::size_t index, UnitId victim, UnitId attacker, float amount, bool paralyzer, float& result)
        {
            auto& package = packages[index];
            if (package.disabled)
            {
                return false;
            }
            auto* state = package.state;
            lua_getglobal(state, "hook_damage_v1");
            if (!lua_isfunction(state, -1))
            {
                lua_pop(state, 1);
                return false;
            }
            lua_pushinteger(state, victim.value);
            lua_pushinteger(state, attacker.value);
            lua_pushnumber(state, amount);
            lua_pushboolean(state, paralyzer);
            lua_pushnumber(state, result);
            package.remaining = limits.instructionBudgetPerCall;
            if (lua_pcall(state, 5, 1, 0) != LUA_OK)
            {
                const char* message = lua_tostring(state, -1);
                disable(package, message ? message : "hook failed");
                lua_pop(state, 1);
                return false;
            }
            if (!lua_isnumber(state, -1))
            {
                disable(package, "hook_damage_v1 did not return a number");
                lua_pop(state, 1);
                return false;
            }
            const auto value = static_cast<float>(lua_tonumber(state, -1));
            lua_pop(state, 1);
            if (!std::isfinite(value) || value < 0.0f)
            {
                disable(package, "hook_damage_v1 returned an invalid damage value");
                return false;
            }
            result = value;
            return true;
        }

        void callTick(std::size_t index, unsigned int tick)
        {
            auto& package = packages[index];
            if (package.disabled)
            {
                return;
            }
            auto* state = package.state;
            lua_getglobal(state, "on_tick");
            if (!lua_isfunction(state, -1))
            {
                lua_pop(state, 1);
                return;
            }
            lua_pushinteger(state, tick);
            package.remaining = limits.instructionBudgetPerCall;
            if (lua_pcall(state, 1, 0, 0) != LUA_OK)
            {
                const char* message = lua_tostring(state, -1);
                disable(package, message ? message : "on_tick failed");
                lua_pop(state, 1);
            }
        }

        std::vector<Package> packages;
        std::vector<std::unique_ptr<Context>> contexts;
        ModStateStore* store;
        ModRuntimeLimits limits;
        std::vector<std::string> logLines;
    };

    ModRuntime::ModRuntime(std::vector<ModRuntimePackage> packages, std::uint32_t seed, ModStateStore& store, ModRuntimeLimits limits)
        : impl_(std::make_unique<Impl>(std::move(packages), seed, store, limits))
    {
    }

    ModRuntime::~ModRuntime() = default;

    void ModRuntime::onTick(unsigned int tick)
    {
        for (std::size_t i = 0; i < impl_->packages.size(); ++i)
        {
            impl_->callTick(i, tick);
        }
    }

    float ModRuntime::adjustDamage(UnitId victim, UnitId attacker, float amount, bool paralyzer)
    {
        auto result = amount;
        for (std::size_t i = 0; i < impl_->packages.size(); ++i)
        {
            impl_->callDamage(i, victim, attacker, amount, paralyzer, result);
        }
        return result;
    }

    ModStateStore& ModRuntime::store() { return *impl_->store; }
    const ModStateStore& ModRuntime::store() const { return *impl_->store; }

    const std::vector<std::string>& ModRuntime::log() const { return impl_->logLines; }

    std::vector<std::string> ModRuntime::disabledPackages() const
    {
        std::vector<std::string> disabled;
        for (const auto& package : impl_->packages)
        {
            if (package.disabled)
            {
                disabled.push_back(package.spec.id);
            }
        }
        return disabled;
    }

    json ModRuntime::toJson() const
    {
        json root;
        root["format"] = "rwe.mod-runtime";
        root["version"] = 1;
        root["disabled"] = disabledPackages();


        json rng = json::array();
        for (const auto& package : impl_->packages)
        {
            std::ostringstream text;
            text << package.rng;
            rng.push_back({{"id", package.spec.id}, {"state", text.str()}});
        }
        root["rng"] = std::move(rng);
        root["store"] = impl_->store->toJson();
        return root;
    }

    void ModRuntime::loadJson(const json& value)
    {
        if (!value.is_object() || value.value("format", std::string()) != "rwe.mod-runtime"
            || value.value("version", 0) != 1)
        {
            throw std::runtime_error("mod runtime: not a version-1 runtime state");
        }
        const auto disabled = value.value("disabled", std::vector<std::string>());
        std::set<std::string> disabledSet(disabled.begin(), disabled.end());
        for (auto& package : impl_->packages)
        {
            package.disabled = disabledSet.count(package.spec.id) != 0;
        }
        if (value.contains("rng"))
        {
            for (const auto& entry : value["rng"])
            {
                const auto id = entry.value("id", std::string());
                for (auto& package : impl_->packages)
                {
                    if (package.spec.id == id)
                    {
                        std::istringstream text(entry.value("state", std::string()));
                        text >> package.rng;
                        break;
                    }
                }
            }
        }
        if (value.contains("store"))
        {
            impl_->store->loadJson(value["store"]);
        }
    }

    std::string ModRuntime::hash() const
    {
        return sha256Hex(toJson().dump(-1, ' ', false));
    }
}
