#include "ModAiController.h"

#include "ModAiScript.h"
#include "ModLuaValues.h"
#include <rwe/mod/ModStateStore.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <random>
#include <rwe/sim/GameSimulation.h>
#include <rwe/util/Sha256.h>
#include <sstream>
#include <stdexcept>
#include <utility>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

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

    class ModAiController::Impl
    {
    public:
        struct OwnUnit
        {
            UnitId id;
            std::string type;
            float x = 0.0f;
            float z = 0.0f;
            unsigned int health = 0;
        };

        Impl(
            PlayerId playerId,
            const ModAiScript& script,
            std::uint64_t rngSeed,
            const ModAiLimits& limits)
            : playerId(playerId), packageId(script.packageId), limits(limits)
        {
            // The seat's own stream. The script hash keeps two ai packages
            // apart, the request's seed carries the resolved-set identity,
            // and the simulation's draw (called once per seat by AiSeat)
            // keeps a replay's sequence attached to the seeded simulation.
            const auto digest = sha256Hex(packageId);
            const auto idSeed = static_cast<std::uint32_t>(std::stoul(digest.substr(0, 8), nullptr, 16));
            auto mixed = static_cast<std::uint32_t>(rngSeed) ^ idSeed ^ script.seed;
            if (mixed == 0)
            {
                mixed = 1;
            }
            rng.seed(mixed);

            memory.limit = limits.memoryBudget;
            open(script);
        }

        ~Impl()
        {
            if (state != nullptr)
            {
                lua_close(state);
            }
        }

        void disable(const std::string& why)
        {
            if (disabledFlag)
            {
                return;
            }
            disabledFlag = true;
            lastErrorText = why;
            addLog("lua ai " + packageId + " disabled: " + why);
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

        void open(const ModAiScript& script)
        {
            state = lua_newstate(limitedAllocator, &memory);
            if (state == nullptr)
            {
                throw std::runtime_error(
                    "Mod " + packageId + ": cannot create a Lua state within its memory budget");
            }
            *static_cast<Impl**>(lua_getextraspace(state)) = this;

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

            lua_createtable(state, 0, 9);
            lua_pushinteger(state, currentModAiApiVersion);
            lua_setfield(state, -2, "api_version");
            lua_pushcfunction(state, cmdAttackFunction);
            lua_setfield(state, -2, "cmd_attack");
            lua_pushcfunction(state, cmdMoveFunction);
            lua_setfield(state, -2, "cmd_move");
            lua_pushcfunction(state, cmdStopFunction);
            lua_setfield(state, -2, "cmd_stop");
            lua_pushcfunction(state, logFunction);
            lua_setfield(state, -2, "log");
            lua_pushcfunction(state, myUnitsFunction);
            lua_setfield(state, -2, "my_units");
            lua_createtable(state, 0, static_cast<int>(
                script.options.is_object() ? script.options.size() : 0));
            if (script.options.is_object())
            {
                for (const auto& [key, value] : script.options.items())
                {
                    pushJson(state, value);
                    lua_setfield(state, -2, key.c_str());
                }
            }
            lua_setfield(state, -2, "options");
            lua_pushcfunction(state, randBelowFunction);
            lua_setfield(state, -2, "rand_below");
            lua_pushcfunction(state, tickFunction);
            lua_setfield(state, -2, "tick");
            lua_pushcfunction(state, storeGetFunction);
            lua_setfield(state, -2, "store_get");
            lua_pushcfunction(state, storeSetFunction);
            lua_setfield(state, -2, "store_set");
            lua_setglobal(state, "ai");

            // Load the package. A script that does not load is a broken
            // package, and the seat is disabled rather than left silently
            // inert with no explanation.
            remaining = limits.instructionBudgetPerCall;
            lua_sethook(state, instructionHook, LUA_MASKCOUNT, hookInstructionCount);
            const int loadStatus = luaL_loadbufferx(state, script.source.data(), script.source.size(),
                script.scriptName.c_str(), "t");
            if (loadStatus != LUA_OK)
            {
                const char* message = lua_tostring(state, -1);
                disable(message != nullptr ? message : "script did not load");
                lua_pop(state, 1);
                return;
            }
            if (lua_pcall(state, 0, 0, 0) != LUA_OK)
            {
                const char* message = lua_tostring(state, -1);
                disable(message != nullptr ? message : "script failed at load");
                lua_pop(state, 1);
                return;
            }
            lua_getglobal(state, "on_tick");
            const bool hasTick = lua_isfunction(state, -1);
            lua_pop(state, 1);
            if (!hasTick)
            {
                disable("script does not define on_tick(tick)");
            }
        }

        void tick(const GameSimulation& sim, const AiTuningProfile& profile, std::vector<PlayerCommand>& outCommands)
        {
            // Watching rather than playing: a replay's command stream is the
            // only producer. An idle scripted seat must not think at all.
            if (disabledFlag || profile.idle || state == nullptr)
            {
                return;
            }

            currentTick = sim.gameTime.value;
            snapshot.clear();
            for (const auto& [id, unit] : sim.units)
            {
                if (unit.owner != playerId || !unit.isAlive())
                {
                    continue;
                }
                snapshot.push_back({id, unit.unitType,
                    simScalarToFloat(unit.position.x), simScalarToFloat(unit.position.z),
                    unit.hitPoints});
            }
            std::sort(snapshot.begin(), snapshot.end(), [](const OwnUnit& a, const OwnUnit& b) {
                return a.id.value < b.id.value;
            });

            pending.clear();
            activeSim = &sim;
            lua_getglobal(state, "on_tick");
            if (!lua_isfunction(state, -1))
            {
                lua_pop(state, 1);
                activeSim = nullptr;
                return;
            }
            lua_pushinteger(state, static_cast<lua_Integer>(currentTick));
            remaining = limits.instructionBudgetPerCall;
            const auto status = lua_pcall(state, 1, 0, 0);
            activeSim = nullptr;
            if (status != LUA_OK)
            {
                const char* message = lua_tostring(state, -1);
                disable(message != nullptr ? message : "on_tick failed");
                lua_pop(state, 1);
                // A faulting tick did not complete; none of its partially
                // collected commands are emitted.
                pending.clear();
                return;
            }

            // Commands leave in sorted unit-id order whatever order the script
            // issued them in, and a unit's own sequence keeps its order.
            std::stable_sort(pending.begin(), pending.end(),
                [](const PlayerUnitCommand& a, const PlayerUnitCommand& b) {
                    return a.unit.value < b.unit.value;
                });
            outCommands.insert(outCommands.end(), pending.begin(), pending.end());
        }

        UnitId checkedOwnUnit(lua_State* state, int index, const char* where)
        {
            const auto raw = luaL_checkinteger(state, index);
            if (raw < 0 || static_cast<unsigned long long>(raw) > std::numeric_limits<unsigned int>::max())
            {
                luaL_error(state, "%s: no such unit id", where);
            }
            const auto id = static_cast<unsigned int>(raw);
            for (const auto& unit : snapshot)
            {
                if (unit.id.value == id)
                {
                    return unit.id;
                }
            }
            luaL_error(state, "%s: unit %d is not owned by this seat", where, static_cast<int>(id));
            return UnitId(0);
        }

        bool pushCommand(PlayerUnitCommand command)
        {
            if (pending.size() >= limits.maxCommandsPerTick)
            {
                return false;
            }
            pending.push_back(std::move(command));
            return true;
        }

        static Impl* current(lua_State* state)
        {
            return *static_cast<Impl**>(lua_getextraspace(state));
        }

        static void pushJson(lua_State* state, const nlohmann::json& value)
        {
            switch (value.type())
            {
            case nlohmann::json::value_t::boolean:
                lua_pushboolean(state, value.get<bool>());
                break;
            case nlohmann::json::value_t::number_integer:
            case nlohmann::json::value_t::number_unsigned:
            case nlohmann::json::value_t::number_float:
                lua_pushnumber(state, value.get<double>());
                break;
            case nlohmann::json::value_t::string:
                lua_pushlstring(state, value.get_ref<const std::string&>().data(),
                    value.get_ref<const std::string&>().size());
                break;
            default:
                lua_pushnil(state);
                break;
            }
        }

        static void instructionHook(lua_State* state, lua_Debug*)
        {
            auto* impl = current(state);
            if (impl->remaining <= hookInstructionCount)
            {
                impl->remaining = 0;
                luaL_error(state, "instruction budget exhausted");
            }
            impl->remaining -= hookInstructionCount;
        }

        // ai.store_get(key) -> value or nil, from this seat's own store.
        static int storeGetFunction(lua_State* state)
        {
            auto* impl = current(state);
            std::size_t keyLength = 0;
            const char* keyText = luaL_checklstring(state, 1, &keyLength);
            pushModLuaValue(state, impl->store.getGlobal(impl->packageId, std::string(keyText, keyLength)));
            return 1;
        }

        // ai.store_set(key, value). The seat's store is saved and hashed
        // (MOD-SDK-021); a Lua variable is neither.
        static int storeSetFunction(lua_State* state)
        {
            auto* impl = current(state);
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
                stored = impl->store.setGlobal(impl->packageId, std::string(keyText, keyLength), modLuaValueAt(state, 2, kind));
            }
            if (!stored)
            {
                return luaL_error(state, "store quota exceeded");
            }
            return 0;
        }

        static int tickFunction(lua_State* state)
        {
            lua_pushinteger(state, static_cast<lua_Integer>(current(state)->currentTick));
            return 1;
        }

        static int myUnitsFunction(lua_State* state)
        {
            auto* impl = current(state);
            lua_createtable(state, static_cast<int>(impl->snapshot.size()), 0);
            int index = 1;
            for (const auto& unit : impl->snapshot)
            {
                lua_createtable(state, 0, 5);
                lua_pushinteger(state, static_cast<lua_Integer>(unit.id.value));
                lua_setfield(state, -2, "id");
                lua_pushlstring(state, unit.type.data(), unit.type.size());
                lua_setfield(state, -2, "type");
                lua_pushnumber(state, unit.x);
                lua_setfield(state, -2, "x");
                lua_pushnumber(state, unit.z);
                lua_setfield(state, -2, "z");
                lua_pushinteger(state, static_cast<lua_Integer>(unit.health));
                lua_setfield(state, -2, "health");
                lua_rawseti(state, -2, index++);
            }
            return 1;
        }

        static int cmdMoveFunction(lua_State* state)
        {
            auto* impl = current(state);
            const auto unit = impl->checkedOwnUnit(state, 1, "ai.cmd_move");
            const auto x = static_cast<float>(luaL_checknumber(state, 2));
            const auto z = static_cast<float>(luaL_checknumber(state, 3));
            if (!std::isfinite(x) || !std::isfinite(z))
            {
                return luaL_error(state, "ai.cmd_move: destination must be finite");
            }
            if (!impl->pushCommand(
                    PlayerUnitCommand(unit, PlayerUnitCommand::IssueOrder(
                        MoveOrder(SimVector(SimScalar(x), 0_ss, SimScalar(z))),
                        PlayerUnitCommand::IssueOrder::Immediate))))
            {
                return luaL_error(state, "ai.cmd_move: command budget exhausted");
            }
            return 0;
        }

        static int cmdStopFunction(lua_State* state)
        {
            auto* impl = current(state);
            const auto unit = impl->checkedOwnUnit(state, 1, "ai.cmd_stop");
            if (!impl->pushCommand(PlayerUnitCommand(unit, PlayerUnitCommand::Stop{})))
            {
                return luaL_error(state, "ai.cmd_stop: command budget exhausted");
            }
            return 0;
        }

        static int cmdAttackFunction(lua_State* state)
        {
            auto* impl = current(state);
            const auto unit = impl->checkedOwnUnit(state, 1, "ai.cmd_attack");
            const auto rawTarget = luaL_checkinteger(state, 2);
            if (rawTarget < 0 || static_cast<unsigned long long>(rawTarget) > std::numeric_limits<unsigned int>::max())
            {
                return luaL_error(state, "ai.cmd_attack: no such unit id");
            }
            const UnitId target(static_cast<unsigned int>(rawTarget));
            // The target may be any unit the simulation knows; what the seat
            // cannot do is command somebody else's unit.
            if (impl->activeSim == nullptr || !impl->activeSim->tryGetUnitState(target))
            {
                return luaL_error(state, "ai.cmd_attack: no such unit %d", static_cast<int>(target.value));
            }
            if (!impl->pushCommand(
                    PlayerUnitCommand(unit, PlayerUnitCommand::IssueOrder(
                        AttackOrder(target), PlayerUnitCommand::IssueOrder::Immediate))))
            {
                return luaL_error(state, "ai.cmd_attack: command budget exhausted");
            }
            return 0;
        }

        static int logFunction(lua_State* state)
        {
            auto* impl = current(state);
            std::size_t length = 0;
            const char* text = luaL_checklstring(state, 1, &length);
            impl->addLog(std::string(text, length));
            return 0;
        }

        static int randBelowFunction(lua_State* state)
        {
            auto* impl = current(state);
            const auto bound = luaL_checkinteger(state, 1);
            if (bound <= 0)
            {
                return luaL_error(state, "ai.rand_below needs a positive bound");
            }
            // Deterministic modulo, as ModRuntime and SimRandom do.
            lua_pushinteger(state, static_cast<lua_Integer>(
                impl->rng() % static_cast<unsigned int>(bound)));
            return 1;
        }

        PlayerId playerId;
        std::string packageId;
        ModAiLimits limits;
        LuaMemory memory;
        lua_State* state = nullptr;
        std::uint64_t remaining = 0;
        std::minstd_rand rng;
        bool disabledFlag = false;
        // The seat's persistent state (MOD-SDK-021). Its own, not the
        // simulation's: tick() sees the simulation as const.
        ModStateStore store;
        std::string lastErrorText;
        std::vector<std::string> logLines;
        unsigned int currentTick = 0;
        const GameSimulation* activeSim = nullptr;
        std::vector<OwnUnit> snapshot;
        std::vector<PlayerUnitCommand> pending;
    };

    ModAiController::ModAiController(
        PlayerId playerId,
        std::shared_ptr<const ModAiScript> script,
        AiTuningProfile profile,
        std::uint64_t rngSeed,
        ModAiLimits limits)
        : playerId_(playerId), profile_(std::move(profile))
    {
        if (!script)
        {
            // The engine is only selected for a seat whose script resolved,
            // so this is a programming error rather than a missing package.
            throw std::runtime_error(
                "Player " + std::to_string(playerId.value)
                + ": the lua engine has no resolved AI script");
        }
        impl_ = std::make_unique<Impl>(playerId, *script, rngSeed, limits);
    }

    ModAiController::~ModAiController() = default;

    void ModAiController::tick(const GameSimulation& sim, std::vector<PlayerCommand>& outCommands)
    {
        impl_->tick(sim, profile_, outCommands);
    }

    bool ModAiController::disabled() const { return impl_->disabledFlag; }

    const std::string& ModAiController::lastError() const { return impl_->lastErrorText; }

    const std::vector<std::string>& ModAiController::log() const { return impl_->logLines; }

    std::string ModAiController::rngState() const
    {
        std::ostringstream text;
        text << impl_->rng;
        return text.str();
    }

    nlohmann::json ModAiController::saveState() const
    {
        return nlohmann::json{
            {"format", "rwe.lua-ai"},
            {"version", 1},
            {"package", impl_->packageId},
            {"rng", rngState()},
            {"disabled", impl_->disabledFlag},
            {"store", impl_->store.toJson()},
        };
    }

    void ModAiController::loadState(const nlohmann::json& state)
    {
        if (!state.is_object() || state.value("format", std::string()) != "rwe.lua-ai" || state.value("version", 0) != 1)
        {
            throw std::runtime_error("lua ai: not a version-1 seat state");
        }
        if (state.value("package", std::string()) != impl_->packageId)
        {
            throw std::runtime_error("lua ai: the saved seat played " + state.value("package", std::string())
                + ", this seat plays " + impl_->packageId);
        }
        if (!state.contains("rng") || !state["rng"].is_string() || !state.contains("disabled")
            || !state["disabled"].is_boolean() || !state.contains("store"))
        {
            throw std::runtime_error("lua ai: the saved seat state is incomplete");
        }
        std::minstd_rand rng;
        std::istringstream text(state["rng"].get<std::string>());
        text >> rng;
        if (text.fail())
        {
            throw std::runtime_error("lua ai: the saved random state does not parse");
        }
        // The store's own load is transactional; the rest is assigned only
        // once everything has parsed, so a bad state changes nothing.
        impl_->store.loadJson(state["store"]);
        impl_->rng = rng;
        if (state["disabled"].get<bool>())
        {
            impl_->disable("disabled before the game was saved");
        }
    }

    std::string ModAiController::stateHash() const
    {
        return sha256Hex(saveState().dump());
    }
}
