#include "ModRuntime.h"
#include "ModLuaValues.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <rwe/util/Sha256.h>

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
            lua_pushcfunction(state, tickFunction);
            lua_setfield(state, -2, "tick");
            lua_pushcfunction(state, unitGetFunction);
            lua_setfield(state, -2, "unit_get");
            lua_pushcfunction(state, unitSetFunction);
            lua_setfield(state, -2, "unit_set");
            lua_pushcfunction(state, unitInfoFunction);
            lua_setfield(state, -2, "unit_info");
            lua_pushcfunction(state, healFunction);
            lua_setfield(state, -2, "heal");
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

        static bool unitIdAt(lua_State* state, int index, UnitId& out)
        {
            if (!lua_isinteger(state, index))
            {
                return false;
            }
            const auto raw = lua_tointeger(state, index);
            if (raw < 0 || raw > static_cast<lua_Integer>(std::numeric_limits<unsigned int>::max()))
            {
                return false;
            }
            out = UnitId(static_cast<unsigned int>(raw));
            return true;
        }

        // rwe.store_get(key) -> value or nil, from this package's global
        // namespace.
        static int storeGetFunction(lua_State* state)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            auto* impl = context->impl;
            std::size_t keyLength = 0;
            const char* keyText = luaL_checklstring(state, 1, &keyLength);
            pushModLuaValue(state,
                impl->store->getGlobal(impl->packages[context->index].spec.id, std::string(keyText, keyLength)));
            return 1;
        }

        // rwe.store_set(key, value). Written in the same shape as unit_set,
        // with no C++ object alive at any luaL_error (MOD-SDK-016).
        static int storeSetFunction(lua_State* state)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            auto* impl = context->impl;
            std::size_t keyLength = 0;
            const char* keyText = luaL_checklstring(state, 1, &keyLength);
            const auto kind = modLuaValueKind(state, 2);
            if (kind == ModLuaValueKind::Invalid)
            {
                return luaL_error(state, "store_set value must be a number, string or boolean");
            }
            if (kind == ModLuaValueKind::NonFinite)
            {
                return luaL_error(state, "store_set value must be finite");
            }
            bool stored = false;
            {
                stored = impl->store->setGlobal(
                    impl->packages[context->index].spec.id, std::string(keyText, keyLength), modLuaValueAt(state, 2, kind));
            }
            if (!stored)
            {
                return luaL_error(state, "store quota exceeded");
            }
            return 0;
        }

        // rwe.tick() -> the simulation tick the current call runs in.
        static int tickFunction(lua_State* state)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            lua_pushinteger(state, static_cast<lua_Integer>(context->impl->currentTick));
            return 1;
        }

        // rwe.unit_info(unit) -> a table describing the unit, or nil when
        // there is no such unit (or nothing to ask).
        static int unitInfoFunction(lua_State* state)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            auto* impl = context->impl;
            UnitId unit(0);
            if (!unitIdAt(state, 1, unit))
            {
                return luaL_error(state, "unit_info needs a unit id");
            }
            ModUnitInfo info;
            if (!impl->world.unitInfo || !impl->world.unitInfo(impl->world.context, unit, info))
            {
                lua_pushnil(state);
                return 1;
            }
            lua_createtable(state, 0, 9);
            lua_pushinteger(state, static_cast<lua_Integer>(info.owner.value));
            lua_setfield(state, -2, "owner");
            lua_pushlstring(state, info.unitType.data(), info.unitType.size());
            lua_setfield(state, -2, "type");
            lua_pushinteger(state, static_cast<lua_Integer>(info.hitPoints));
            lua_setfield(state, -2, "health");
            lua_pushinteger(state, static_cast<lua_Integer>(info.maxHitPoints));
            lua_setfield(state, -2, "max_health");
            lua_pushboolean(state, info.complete);
            lua_setfield(state, -2, "complete");
            lua_pushboolean(state, info.dead);
            lua_setfield(state, -2, "dead");
            lua_pushnumber(state, info.x);
            lua_setfield(state, -2, "x");
            lua_pushnumber(state, info.y);
            lua_setfield(state, -2, "y");
            lua_pushnumber(state, info.z);
            lua_setfield(state, -2, "z");
            return 1;
        }

        // rwe.heal(unit, amount): queue hit points for a living, finished
        // unit. Applied at the end of the tick, capped at its maximum.
        static int healFunction(lua_State* state)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            auto* impl = context->impl;
            UnitId unit(0);
            if (!unitIdAt(state, 1, unit))
            {
                return luaL_error(state, "heal needs a unit id");
            }
            if (!lua_isinteger(state, 2) || lua_tointeger(state, 2) < 0
                || lua_tointeger(state, 2) > static_cast<lua_Integer>(1000000000))
            {
                return luaL_error(state, "heal needs a whole amount from 0 to 1000000000");
            }
            if (impl->world.unitExists && !impl->world.unitExists(impl->world.context, unit))
            {
                return luaL_error(state, "heal: no unit %d", static_cast<int>(unit.value));
            }
            if (impl->effects.size() >= impl->limits.maxPendingEffects)
            {
                return luaL_error(state, "too many pending effects");
            }
            {
                ModEffect effect;
                effect.kind = ModEffect::Kind::Heal;
                effect.packageId = impl->packages[context->index].spec.id;
                effect.unit = unit;
                effect.amount = static_cast<unsigned int>(lua_tointeger(state, 2));
                impl->effects.push_back(std::move(effect));
            }
            return 0;
        }

        // rwe.unit_get(unit, key) -> value or nil, from this package's
        // namespace in the per-unit scope.
        static int unitGetFunction(lua_State* state)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            auto* impl = context->impl;
            UnitId unit(0);
            if (!unitIdAt(state, 1, unit))
            {
                return luaL_error(state, "unit_get needs a unit id");
            }
            std::size_t keyLength = 0;
            const char* keyText = luaL_checklstring(state, 2, &keyLength);
            pushModLuaValue(state,
                impl->store->getUnit(impl->packages[context->index].spec.id, unit, std::string(keyText, keyLength)));
            return 1;
        }

        // rwe.unit_set(unit, key, value). Refused for a unit that does not
        // exist: an entry for a free id would be inherited by the next unit
        // built in that slot. Entries go with the unit (deleteDeadUnits).
        static int unitSetFunction(lua_State* state)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            auto* impl = context->impl;
            UnitId unit(0);
            if (!unitIdAt(state, 1, unit))
            {
                return luaL_error(state, "unit_set needs a unit id");
            }
            std::size_t keyLength = 0;
            const char* keyText = luaL_checklstring(state, 2, &keyLength);
            const auto kind = modLuaValueKind(state, 3);
            if (kind == ModLuaValueKind::Invalid)
            {
                return luaL_error(state, "unit_set value must be a number, string or boolean");
            }
            if (kind == ModLuaValueKind::NonFinite)
            {
                return luaL_error(state, "unit_set value must be finite");
            }
            if (impl->world.unitExists && !impl->world.unitExists(impl->world.context, unit))
            {
                return luaL_error(state, "unit_set: no unit %d", static_cast<int>(unit.value));
            }
            bool stored = false;
            {
                stored = impl->store->setUnit(impl->packages[context->index].spec.id,
                    unit,
                    std::string(keyText, keyLength),
                    modLuaValueAt(state, 3, kind));
            }
            if (!stored)
            {
                return luaL_error(state, "store quota exceeded");
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

        // Calls `name` in one package if it exports it. pushArguments pushes
        // the arguments and returns how many; nothing it pushes is C++.
        template <typename PushArguments>
        void callEvent(std::size_t index, const char* name, PushArguments pushArguments)
        {
            auto& package = packages[index];
            if (package.disabled)
            {
                return;
            }
            auto* state = package.state;
            lua_getglobal(state, name);
            if (!lua_isfunction(state, -1))
            {
                lua_pop(state, 1);
                return;
            }
            const int arguments = pushArguments(state);
            package.remaining = limits.instructionBudgetPerCall;
            if (lua_pcall(state, arguments, 0, 0) != LUA_OK)
            {
                const char* message = lua_tostring(state, -1);
                disable(package, message ? message : std::string(name) + " failed");
                lua_pop(state, 1);
            }
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
        // Not runtime state: the caller sets it from the simulation's own
        // clock before every dispatch, so nothing about it needs saving.
        unsigned int currentTick = 0;
        ModRuntimeWorld world;
        // Runtime state: saved, hashed and restored with the rest.
        std::vector<ModEffect> effects;
    };

    ModRuntime::ModRuntime(std::vector<ModRuntimePackage> packages, std::uint32_t seed, ModStateStore& store, ModRuntimeLimits limits)
        : impl_(std::make_unique<Impl>(std::move(packages), seed, store, limits))
    {
    }

    ModRuntime::~ModRuntime() = default;

    void ModRuntime::bind(unsigned int tick, const ModRuntimeWorld& world)
    {
        impl_->currentTick = tick;
        impl_->world = world;
    }

    void ModRuntime::bind(unsigned int tick, const void* context, ModRuntimeUnitExists unitExists)
    {
        bind(tick, ModRuntimeWorld{context, unitExists, nullptr});
    }

    void ModRuntime::onTick(unsigned int tick)
    {
        impl_->currentTick = tick;
        for (std::size_t i = 0; i < impl_->packages.size(); ++i)
        {
            impl_->callTick(i, tick);
        }
    }

    void ModRuntime::onUnitCreated(UnitId unit, PlayerId owner, const std::string& unitType)
    {
        for (std::size_t i = 0; i < impl_->packages.size(); ++i)
        {
            impl_->callEvent(i, "on_unit_created", [&](lua_State* state) {
                lua_pushinteger(state, static_cast<lua_Integer>(unit.value));
                lua_pushinteger(state, static_cast<lua_Integer>(owner.value));
                lua_pushlstring(state, unitType.data(), unitType.size());
                return 3;
            });
        }
    }

    void ModRuntime::onUnitCompleted(UnitId unit)
    {
        for (std::size_t i = 0; i < impl_->packages.size(); ++i)
        {
            impl_->callEvent(i, "on_unit_completed", [&](lua_State* state) {
                lua_pushinteger(state, static_cast<lua_Integer>(unit.value));
                return 1;
            });
        }
    }

    void ModRuntime::onUnitKilled(UnitId unit, std::optional<UnitId> attacker)
    {
        for (std::size_t i = 0; i < impl_->packages.size(); ++i)
        {
            impl_->callEvent(i, "on_unit_killed", [&](lua_State* state) {
                lua_pushinteger(state, static_cast<lua_Integer>(unit.value));
                if (attacker)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(attacker->value));
                }
                else
                {
                    lua_pushnil(state);
                }
                return 2;
            });
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

    std::vector<ModEffect> ModRuntime::takeEffects()
    {
        std::vector<ModEffect> taken;
        taken.swap(impl_->effects);
        return taken;
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
        if (!impl_->effects.empty())
        {
            json effects = json::array();
            for (const auto& effect : impl_->effects)
            {
                effects.push_back({{"kind", "heal"},
                    {"package", effect.packageId},
                    {"unit", effect.unit.value},
                    {"amount", effect.amount}});
            }
            root["effects"] = std::move(effects);
        }
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
        std::vector<ModEffect> effects;
        if (value.contains("effects"))
        {
            if (!value["effects"].is_array() || value["effects"].size() > impl_->limits.maxPendingEffects)
            {
                throw std::runtime_error("mod runtime: effects must be an array within the pending limit");
            }
            for (const auto& item : value["effects"])
            {
                if (!item.is_object() || item.value("kind", std::string()) != "heal"
                    || !item.contains("unit") || !item["unit"].is_number_unsigned()
                    || !item.contains("amount") || !item["amount"].is_number_unsigned())
                {
                    throw std::runtime_error("mod runtime: malformed pending effect");
                }
                ModEffect effect;
                effect.packageId = item.value("package", std::string());
                effect.unit = UnitId(item["unit"].get<unsigned int>());
                effect.amount = item["amount"].get<unsigned int>();
                effects.push_back(std::move(effect));
            }
        }
        if (value.contains("store"))
        {
            impl_->store->loadJson(value["store"]);
        }
        impl_->effects = std::move(effects);
    }

    std::string ModRuntime::hash() const
    {
        return sha256Hex(toJson().dump(-1, ' ', false));
    }
}
