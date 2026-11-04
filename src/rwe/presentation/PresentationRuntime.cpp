#include "PresentationRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <utility>

// Lua is built as C++ (MOD-SDK-023), so its headers are included as C++.
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

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

        /**
         * The same bounded allocator ModRuntime uses: a package that runs out
         * of its budget gets a failed allocation rather than the process's
         * memory. Kept here rather than shared so the MOD-08 runtime's tested
         * behaviour does not move as part of a presentation change.
         */
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

        float checkedFinite(lua_State* state, double value, const char* what)
        {
            const auto result = static_cast<float>(value);
            if (!std::isfinite(result))
            {
                luaL_error(state, "%s must be finite", what);
            }
            return result;
        }

        void pushJson(lua_State* state, const nlohmann::json& value)
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
                lua_pushlstring(state, value.get_ref<const std::string&>().data(), value.get_ref<const std::string&>().size());
                break;
            default:
                lua_pushnil(state);
                break;
            }
        }

        Color readColor(lua_State* state, int index, const Color& fallback)
        {
            if (!lua_istable(state, index))
            {
                return fallback;
            }

            auto component = [&](const char* name, int fallbackValue) {
                lua_getfield(state, index, name);
                int value = fallbackValue;
                if (lua_isnumber(state, -1))
                {
                    const auto number = lua_tonumber(state, -1);
                    if (std::isfinite(number))
                    {
                        value = static_cast<int>(std::lround(std::clamp(number, 0.0, 255.0)));
                    }
                }
                lua_pop(state, 1);
                return static_cast<unsigned char>(std::clamp(value, 0, 255));
            };

            return Color(
                component("r", 255),
                component("g", 255),
                component("b", 255),
                component("a", 255));
        }

        void pushUnitTable(lua_State* state, const PresentationUnit& unit)
        {
            lua_newtable(state);
            lua_pushinteger(state, unit.id.value);
            lua_setfield(state, -2, "id");
            lua_pushlstring(state, unit.type.data(), unit.type.size());
            lua_setfield(state, -2, "type");
            lua_pushinteger(state, unit.owner.value);
            lua_setfield(state, -2, "owner");
            if (unit.teamId)
            {
                lua_pushinteger(state, *unit.teamId);
            }
            else
            {
                lua_pushnil(state);
            }
            lua_setfield(state, -2, "team");
            if (unit.color)
            {
                lua_pushinteger(state, unit.color->value);
            }
            else
            {
                lua_pushnil(state);
            }
            lua_setfield(state, -2, "color");

            lua_pushnumber(state, unit.position.x.value);
            lua_setfield(state, -2, "x");
            lua_pushnumber(state, unit.position.y.value);
            lua_setfield(state, -2, "y");
            lua_pushnumber(state, unit.position.z.value);
            lua_setfield(state, -2, "z");
            lua_pushinteger(state, unit.rotation.value);
            lua_setfield(state, -2, "heading");
            lua_pushnumber(state, toRadians(unit.rotation).value);
            lua_setfield(state, -2, "heading_radians");

            lua_pushinteger(state, static_cast<lua_Integer>(unit.hitPoints));
            lua_setfield(state, -2, "hit_points");
            lua_pushinteger(state, static_cast<lua_Integer>(unit.maxHitPoints));
            lua_setfield(state, -2, "max_hit_points");
            lua_pushboolean(state, unit.owned);
            lua_setfield(state, -2, "owned");
            lua_pushboolean(state, unit.allied);
            lua_setfield(state, -2, "allied");
            lua_pushlstring(state, unit.displayName.data(), unit.displayName.size());
            lua_setfield(state, -2, "name");
        }

        void allOfUnitList(lua_State* state, const std::vector<PresentationUnit>& units)
        {
            lua_createtable(state, static_cast<int>(units.size()), 0);
            int index = 1;
            for (const auto& unit : units)
            {
                pushUnitTable(state, unit);
                lua_rawseti(state, -2, index++);
            }
        }
    }

    class PresentationRuntime::Impl
    {
    public:
        struct Package
        {
            PresentationPackage spec;
            lua_State* state = nullptr;
            LuaMemory memory;
            std::uint64_t remaining = 0;
            bool disabled = false;
        };

        struct Context
        {
            Impl* impl = nullptr;
            std::size_t index = 0;
        };

        Impl(std::vector<PresentationPackage> packagesIn, PresentationRuntimeLimits limits)
            : limits(limits)
        {
            packages.reserve(packagesIn.size());
            contexts.reserve(packagesIn.size());
            for (auto& package : packagesIn)
            {
                Package entry;
                entry.spec = std::move(package);
                entry.memory.limit = limits.memoryBudgetPerPackage;
                packages.push_back(std::move(entry));
            }
            for (std::size_t i = 0; i < packages.size(); ++i)
            {
                auto context = std::make_unique<Context>();
                context->impl = this;
                context->index = i;
                contexts.push_back(std::move(context));
            }
            for (std::size_t i = 0; i < packages.size(); ++i)
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

        void disable(Package& package, const std::string& why)
        {
            if (package.disabled)
            {
                return;
            }
            package.disabled = true;
            addLog("disabling " + package.spec.packageId + ": " + why);
        }

        bool hasDisabled() const
        {
            return std::any_of(packages.begin(), packages.end(), [](const Package& p) { return p.disabled; });
        }

        std::size_t disabledCount() const
        {
            return static_cast<std::size_t>(std::count_if(packages.begin(), packages.end(), [](const Package& p) { return p.disabled; }));
        }

        std::size_t packageCount() const
        {
            return packages.size();
        }

        std::vector<std::string> disabledPackages() const
        {
            std::vector<std::string> result;
            for (const auto& package : packages)
            {
                if (package.disabled)
                {
                    result.push_back(package.spec.packageId);
                }
            }
            return result;
        }

        void openPackage(std::size_t index)
        {
            auto& package = packages[index];
            auto* state = lua_newstate(limitedAllocator, &package.memory);
            if (!state)
            {
                throw std::runtime_error("Mod " + package.spec.packageId + ": cannot create a Lua state within its memory budget");
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
            lua_pushinteger(state, currentPresentationApiVersion);
            lua_setfield(state, -2, "api_version");
            lua_pushcfunction(state, logFunction);
            lua_setfield(state, -2, "log");
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

            const luaL_Reg functions[] = {
                {"units", unitsFunction},
                {"my_units", myUnitsFunction},
                {"unit", unitFunction},
                {"contacts", contactsFunction},
                {"features", featuresFunction},
                {"projectiles", projectilesFunction},
                {"resources", resourcesFunction},
                {"unit_definition", unitDefinitionFunction},
                {"selected", selectedFunction},
                {"camera", cameraFunction},
                {"mouse", mouseFunction},
                {"screen_size", screenSizeFunction},
                {"tick", tickFunction},
                {"is_visible_at", isVisibleAtFunction},
                {"is_explored_at", isExploredAtFunction},
                {"draw_rect", drawRectFunction},
                {"draw_line", drawLineFunction},
                {"draw_text", drawTextFunction},
                {"draw_bar", drawBarFunction},
                {"submit_move", submitMoveFunction},
                {"submit_stop", submitStopFunction},
                {"submit_attack", submitAttackFunction},
            };
            for (const auto& function : functions)
            {
                lua_pushcfunction(state, function.func);
                lua_setfield(state, -2, function.name);
            }
            lua_setglobal(state, "rwe");

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

        static Impl* implOf(lua_State* state)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            return context->impl;
        }

        static int logFunction(lua_State* state)
        {
            auto* context = *static_cast<Context**>(lua_getextraspace(state));
            std::size_t length = 0;
            const char* text = luaL_checklstring(state, 1, &length);
            context->impl->addLog(std::string(text, length));
            return 0;
        }

        static int unitsFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            allOfUnitList(state, impl->frameView ? impl->frameView->units() : std::vector<PresentationUnit>{});
            return 1;
        }

        static int myUnitsFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            std::vector<PresentationUnit> units;
            if (impl->frameView)
            {
                for (auto& unit : impl->frameView->units())
                {
                    if (unit.owned)
                    {
                        units.push_back(std::move(unit));
                    }
                }
            }
            allOfUnitList(state, units);
            return 1;
        }

        static int unitFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            if (!impl->frameView)
            {
                lua_pushnil(state);
                return 1;
            }
            const auto id = UnitId(static_cast<unsigned int>(luaL_checkinteger(state, 1)));
            const auto unit = impl->frameView->unit(id);
            if (!unit)
            {
                lua_pushnil(state);
                return 1;
            }
            pushUnitTable(state, *unit);
            return 1;
        }

        static int contactsFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            const auto contacts = impl->frameView ? impl->frameView->sensorContacts() : std::vector<PresentationSensorContact>{};
            lua_createtable(state, static_cast<int>(contacts.size()), 0);
            int index = 1;
            for (const auto& contact : contacts)
            {
                lua_newtable(state);
                lua_pushinteger(state, contact.id.value);
                lua_setfield(state, -2, "id");
                lua_pushnumber(state, contact.position.x.value);
                lua_setfield(state, -2, "x");
                lua_pushnumber(state, contact.position.z.value);
                lua_setfield(state, -2, "z");
                lua_pushboolean(state, contact.sonar);
                lua_setfield(state, -2, "sonar");
                lua_rawseti(state, -2, index++);
            }
            return 1;
        }

        static int featuresFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            const auto features = impl->frameView ? impl->frameView->features() : std::vector<PresentationFeature>{};
            lua_createtable(state, static_cast<int>(features.size()), 0);
            int index = 1;
            for (const auto& feature : features)
            {
                lua_newtable(state);
                lua_pushinteger(state, feature.id.value);
                lua_setfield(state, -2, "id");
                lua_pushlstring(state, feature.type.data(), feature.type.size());
                lua_setfield(state, -2, "type");
                lua_pushnumber(state, feature.position.x.value);
                lua_setfield(state, -2, "x");
                lua_pushnumber(state, feature.position.z.value);
                lua_setfield(state, -2, "z");
                lua_pushboolean(state, feature.visible);
                lua_setfield(state, -2, "visible");
                lua_rawseti(state, -2, index++);
            }
            return 1;
        }

        static int projectilesFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            const auto projectiles = impl->frameView ? impl->frameView->projectiles() : std::vector<PresentationProjectile>{};
            lua_createtable(state, static_cast<int>(projectiles.size()), 0);
            int index = 1;
            for (const auto& projectile : projectiles)
            {
                lua_newtable(state);
                lua_pushinteger(state, projectile.id.value);
                lua_setfield(state, -2, "id");
                lua_pushnumber(state, projectile.position.x.value);
                lua_setfield(state, -2, "x");
                lua_pushnumber(state, projectile.position.y.value);
                lua_setfield(state, -2, "y");
                lua_pushnumber(state, projectile.position.z.value);
                lua_setfield(state, -2, "z");
                lua_rawseti(state, -2, index++);
            }
            return 1;
        }

        static int resourcesFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            const auto resources = impl->frameView ? impl->frameView->resources() : std::nullopt;
            if (!resources)
            {
                lua_pushnil(state);
                return 1;
            }
            lua_newtable(state);
            lua_pushnumber(state, resources->metal.value);
            lua_setfield(state, -2, "metal");
            lua_pushnumber(state, resources->energy.value);
            lua_setfield(state, -2, "energy");
            lua_pushnumber(state, resources->maxMetal.value);
            lua_setfield(state, -2, "max_metal");
            lua_pushnumber(state, resources->maxEnergy.value);
            lua_setfield(state, -2, "max_energy");
            lua_pushnumber(state, resources->metalProduced.value);
            lua_setfield(state, -2, "metal_produced");
            lua_pushnumber(state, resources->energyProduced.value);
            lua_setfield(state, -2, "energy_produced");
            lua_pushboolean(state, resources->metalStalled);
            lua_setfield(state, -2, "metal_stalled");
            lua_pushboolean(state, resources->energyStalled);
            lua_setfield(state, -2, "energy_stalled");
            return 1;
        }

        static int unitDefinitionFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            if (!impl->frameView)
            {
                lua_pushnil(state);
                return 1;
            }
            std::size_t length = 0;
            const char* type = luaL_checklstring(state, 1, &length);
            const auto definition = impl->frameView->unitDefinition(std::string(type, length));
            if (!definition)
            {
                lua_pushnil(state);
                return 1;
            }
            lua_newtable(state);
            lua_pushlstring(state, definition->name.data(), definition->name.size());
            lua_setfield(state, -2, "name");
            lua_pushlstring(state, definition->displayName.data(), definition->displayName.size());
            lua_setfield(state, -2, "display_name");
            lua_pushlstring(state, definition->description.data(), definition->description.size());
            lua_setfield(state, -2, "description");
            lua_pushnumber(state, definition->buildCostMetal.value);
            lua_setfield(state, -2, "metal");
            lua_pushnumber(state, definition->buildCostEnergy.value);
            lua_setfield(state, -2, "energy");
            lua_pushinteger(state, definition->buildTime);
            lua_setfield(state, -2, "build_time");
            lua_pushinteger(state, definition->maxHitPoints);
            lua_setfield(state, -2, "max_hit_points");
            lua_pushboolean(state, definition->isBuilder);
            lua_setfield(state, -2, "builder");
            lua_pushboolean(state, definition->isMobile);
            lua_setfield(state, -2, "mobile");
            lua_pushboolean(state, definition->canMove);
            lua_setfield(state, -2, "can_move");
            return 1;
        }

        static int selectedFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            static const std::vector<UnitId> noSelection;
            const auto& selected = impl->frameInput ? impl->frameInput->selectedUnits : noSelection;
            lua_createtable(state, static_cast<int>(selected.size()), 0);
            int index = 1;
            for (auto id : selected)
            {
                lua_pushinteger(state, id.value);
                lua_rawseti(state, -2, index++);
            }
            return 1;
        }

        static int cameraFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            const auto& input = impl->emptyInput;
            const auto* in = impl->frameInput ? impl->frameInput : &input;
            lua_newtable(state);
            lua_pushnumber(state, in->cameraX);
            lua_setfield(state, -2, "x");
            lua_pushnumber(state, in->cameraY);
            lua_setfield(state, -2, "y");
            lua_pushnumber(state, in->cameraZ);
            lua_setfield(state, -2, "z");
            lua_pushnumber(state, in->cameraZoom);
            lua_setfield(state, -2, "zoom");
            return 1;
        }

        static int mouseFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            const auto& input = impl->emptyInput;
            const auto* in = impl->frameInput ? impl->frameInput : &input;
            lua_newtable(state);
            lua_pushnumber(state, in->mouseX);
            lua_setfield(state, -2, "x");
            lua_pushnumber(state, in->mouseY);
            lua_setfield(state, -2, "y");
            lua_pushboolean(state, in->mouseInsideWorld);
            lua_setfield(state, -2, "over_world");
            return 1;
        }

        static int screenSizeFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            const auto& input = impl->emptyInput;
            const auto* in = impl->frameInput ? impl->frameInput : &input;
            lua_newtable(state);
            lua_pushnumber(state, in->uiWidth);
            lua_setfield(state, -2, "width");
            lua_pushnumber(state, in->uiHeight);
            lua_setfield(state, -2, "height");
            return 1;
        }

        static int tickFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            lua_pushinteger(state, impl->frameView ? impl->frameView->tick() : 0u);
            return 1;
        }

        static SimVector readWorldPoint(lua_State* state, int first)
        {
            const auto x = checkedFinite(state, luaL_checknumber(state, first), "x");
            const auto z = checkedFinite(state, luaL_checknumber(state, first + 1), "z");
            return SimVector(SimScalar(x), 0_ss, SimScalar(z));
        }

        static int isVisibleAtFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            if (!impl->frameView)
            {
                lua_pushboolean(state, 0);
                return 1;
            }
            lua_pushboolean(state, impl->frameView->isVisibleAt(readWorldPoint(state, 1)));
            return 1;
        }

        static int isExploredAtFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            if (!impl->frameView)
            {
                lua_pushboolean(state, 0);
                return 1;
            }
            lua_pushboolean(state, impl->frameView->isExploredAt(readWorldPoint(state, 1)));
            return 1;
        }

        static int drawRectFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            PresentationDrawCommand command;
            command.kind = PresentationDrawCommand::Kind::Rect;
            command.x = checkedFinite(state, luaL_checknumber(state, 1), "x");
            command.y = checkedFinite(state, luaL_checknumber(state, 2), "y");
            command.width = checkedFinite(state, luaL_checknumber(state, 3), "width");
            command.height = checkedFinite(state, luaL_checknumber(state, 4), "height");
            command.color = readColor(state, 5, Color(255, 255, 255, 255));
            if (command.width > 0.0f && command.height > 0.0f)
            {
                impl->drawList.add(std::move(command));
            }
            return 0;
        }

        static int drawLineFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            PresentationDrawCommand command;
            command.kind = PresentationDrawCommand::Kind::Line;
            command.x = checkedFinite(state, luaL_checknumber(state, 1), "x1");
            command.y = checkedFinite(state, luaL_checknumber(state, 2), "y1");
            command.x2 = checkedFinite(state, luaL_checknumber(state, 3), "x2");
            command.y2 = checkedFinite(state, luaL_checknumber(state, 4), "y2");
            command.color = readColor(state, 5, Color(255, 255, 255, 255));
            if (lua_isnumber(state, 6))
            {
                command.thickness = checkedFinite(state, lua_tonumber(state, 6), "thickness");
            }
            if (command.thickness <= 0.0f)
            {
                return luaL_error(state, "thickness must be positive");
            }
            impl->drawList.add(std::move(command));
            return 0;
        }

        static int drawTextFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            PresentationDrawCommand command;
            command.kind = PresentationDrawCommand::Kind::Text;
            command.x = checkedFinite(state, luaL_checknumber(state, 1), "x");
            command.y = checkedFinite(state, luaL_checknumber(state, 2), "y");
            std::size_t length = 0;
            const char* text = luaL_checklstring(state, 3, &length);
            command.text.assign(text, length);
            command.color = readColor(state, 4, Color(255, 255, 255, 255));
            if (!command.text.empty())
            {
                impl->drawList.add(std::move(command));
            }
            return 0;
        }

        static int drawBarFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            PresentationDrawCommand command;
            command.kind = PresentationDrawCommand::Kind::Bar;
            command.x = checkedFinite(state, luaL_checknumber(state, 1), "x");
            command.y = checkedFinite(state, luaL_checknumber(state, 2), "y");
            command.width = checkedFinite(state, luaL_checknumber(state, 3), "width");
            command.height = checkedFinite(state, luaL_checknumber(state, 4), "height");
            command.fraction = std::clamp(checkedFinite(state, luaL_checknumber(state, 5), "fraction"), 0.0f, 1.0f);
            command.color = readColor(state, 6, Color(255, 255, 255, 255));
            if (command.width > 0.0f && command.height > 0.0f)
            {
                impl->drawList.add(std::move(command));
            }
            return 0;
        }

        /** Collects the ids the mod may order: the viewer's own, visible units. */
        static std::vector<UnitId> readOwnUnits(lua_State* state, int index)
        {
            auto* impl = implOf(state);
            if (!impl->frameView)
            {
                return {};
            }

            luaL_checktype(state, index, LUA_TTABLE);
            std::vector<UnitId> result;
            const auto count = static_cast<int>(lua_rawlen(state, index));
            for (int i = 1; i <= count; ++i)
            {
                lua_rawgeti(state, index, i);
                if (!lua_isinteger(state, -1))
                {
                    lua_pop(state, 1);
                    continue;
                }
                const auto id = UnitId(static_cast<unsigned int>(lua_tointeger(state, -1)));
                lua_pop(state, 1);

                const auto unit = impl->frameView->unit(id);
                if (unit && unit->owned)
                {
                    result.push_back(id);
                }
            }
            return result;
        }

        static int submitMoveFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            auto units = readOwnUnits(state, 1);
            if (units.empty())
            {
                lua_pushinteger(state, 0);
                return 1;
            }
            PresentationCommand command;
            command.kind = PresentationCommand::Kind::Move;
            command.units = std::move(units);
            command.position = readWorldPoint(state, 2);
            impl->commands.push_back(std::move(command));
            lua_pushinteger(state, static_cast<lua_Integer>(impl->commands.back().units.size()));
            return 1;
        }

        static int submitStopFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            auto units = readOwnUnits(state, 1);
            if (units.empty())
            {
                lua_pushinteger(state, 0);
                return 1;
            }
            PresentationCommand command;
            command.kind = PresentationCommand::Kind::Stop;
            command.units = std::move(units);
            impl->commands.push_back(std::move(command));
            lua_pushinteger(state, static_cast<lua_Integer>(impl->commands.back().units.size()));
            return 1;
        }

        static int submitAttackFunction(lua_State* state)
        {
            auto* impl = implOf(state);
            auto units = readOwnUnits(state, 1);
            const auto targetId = UnitId(static_cast<unsigned int>(luaL_checkinteger(state, 2)));
            if (units.empty() || !impl->frameView || !impl->frameView->unit(targetId))
            {
                lua_pushinteger(state, 0);
                return 1;
            }
            PresentationCommand command;
            command.kind = PresentationCommand::Kind::Attack;
            command.units = std::move(units);
            command.target = targetId;
            impl->commands.push_back(std::move(command));
            lua_pushinteger(state, static_cast<lua_Integer>(impl->commands.back().units.size()));
            return 1;
        }

        void beginFrame(const PlayerView& viewIn, const PresentationFrameInput& inputIn)
        {
            drawList.clear();
            frameView = &viewIn;
            frameInput = &inputIn;
            for (std::size_t i = 0; i < packages.size(); ++i)
            {
                callHook(i, "on_frame", [&](lua_State* state) { lua_pushnumber(state, inputIn.deltaSeconds); }, 1, 0);
            }
            frameView = nullptr;
            frameInput = nullptr;
        }

        bool mouseEvent(const char* hook, const PlayerView& viewIn, const PresentationFrameInput& inputIn, int button)
        {
            bool consumed = false;
            frameView = &viewIn;
            frameInput = &inputIn;
            for (std::size_t i = 0; i < packages.size(); ++i)
            {
                consumed = callHook(i, hook, [&](lua_State* state) {
                    lua_pushnumber(state, inputIn.mouseX);
                    lua_pushnumber(state, inputIn.mouseY);
                    lua_pushinteger(state, button);
                }, 3, 1) || consumed;
            }
            frameView = nullptr;
            frameInput = nullptr;
            return consumed;
        }

        void mouseMoveEvent(const char* hook, const PlayerView& viewIn, const PresentationFrameInput& inputIn)
        {
            frameView = &viewIn;
            frameInput = &inputIn;
            for (std::size_t i = 0; i < packages.size(); ++i)
            {
                callHook(i, hook, [&](lua_State* state) {
                    lua_pushnumber(state, inputIn.mouseX);
                    lua_pushnumber(state, inputIn.mouseY);
                }, 2, 0);
            }
            frameView = nullptr;
            frameInput = nullptr;
        }

        bool keyEvent(const char* hook, const PlayerView& viewIn, const PresentationFrameInput& inputIn, int key)
        {
            bool consumed = false;
            frameView = &viewIn;
            frameInput = &inputIn;
            for (std::size_t i = 0; i < packages.size(); ++i)
            {
                consumed = callHook(i, hook, [&](lua_State* state) { lua_pushinteger(state, key); }, 1, 1) || consumed;
            }
            frameView = nullptr;
            frameInput = nullptr;
            return consumed;
        }

        /**
         * Calls one hook in one package. `nresults` is what the caller reads;
         * a numeric result is truthy for the event hooks. A fault disables the
         * package and leaves the caller's answer unchanged.
         */
        template <typename PushArgs>
        bool callHook(std::size_t index, const char* name, PushArgs&& pushArgs, int argCount, int resultCount)
        {
            auto& package = packages[index];
            if (package.disabled)
            {
                return false;
            }
            auto* state = package.state;
            lua_getglobal(state, name);
            if (!lua_isfunction(state, -1))
            {
                lua_pop(state, 1);
                return false;
            }
            pushArgs(state);
            package.remaining = limits.instructionBudgetPerCall;
            if (lua_pcall(state, argCount, resultCount, 0) != LUA_OK)
            {
                const char* message = lua_tostring(state, -1);
                disable(package, message ? message : std::string(name) + " failed");
                lua_pop(state, 1);
                return false;
            }

            bool truthy = false;
            if (resultCount > 0)
            {
                truthy = lua_toboolean(state, -1) != 0;
                lua_pop(state, resultCount);
            }
            return truthy;
        }

        std::vector<Package> packages;
        std::vector<std::unique_ptr<Context>> contexts;
        PresentationRuntimeLimits limits;
        std::vector<std::string> logLines;
        PresentationDrawList drawList;
        std::vector<PresentationCommand> commands;
        const PlayerView* frameView{nullptr};
        const PresentationFrameInput* frameInput{nullptr};
        PresentationFrameInput emptyInput;
    };

    PresentationRuntime::PresentationRuntime(
        std::vector<PresentationPackage> packages,
        PresentationRuntimeLimits limits)
        : impl_(std::make_unique<Impl>(std::move(packages), limits)), limits_(limits)
    {
    }

    PresentationRuntime::~PresentationRuntime() = default;

    bool PresentationRuntime::hasPackages() const
    {
        return impl_->packageCount() != 0;
    }

    void PresentationRuntime::beginFrame(const PlayerView& view, const PresentationFrameInput& input)
    {
        impl_->beginFrame(view, input);
    }

    const PresentationDrawList& PresentationRuntime::drawList() const
    {
        return impl_->drawList;
    }

    std::vector<PresentationCommand> PresentationRuntime::takeCommands()
    {
        return std::exchange(impl_->commands, {});
    }

    bool PresentationRuntime::mouseDown(const PlayerView& view, const PresentationFrameInput& input, int button)
    {
        return impl_->mouseEvent("on_mouse_down", view, input, button);
    }

    bool PresentationRuntime::mouseUp(const PlayerView& view, const PresentationFrameInput& input, int button)
    {
        return impl_->mouseEvent("on_mouse_up", view, input, button);
    }

    void PresentationRuntime::mouseMove(const PlayerView& view, const PresentationFrameInput& input)
    {
        impl_->mouseMoveEvent("on_mouse_move", view, input);
    }

    bool PresentationRuntime::keyDown(const PlayerView& view, const PresentationFrameInput& input, int key)
    {
        return impl_->keyEvent("on_key_down", view, input, key);
    }

    bool PresentationRuntime::reload(std::vector<PresentationPackage> packages)
    {
        std::unique_ptr<Impl> next;
        try
        {
            next = std::make_unique<Impl>(std::move(packages), limits_);
        }
        catch (const std::exception& e)
        {
            impl_->addLog(std::string("presentation reload failed: ") + e.what());
            return false;
        }

        if (next->hasDisabled())
        {
            for (const auto& message : next->disabledPackages())
            {
                impl_->addLog("presentation reload failed: " + message);
            }
            return false;
        }

        impl_ = std::move(next);
        return true;
    }

    const std::vector<std::string>& PresentationRuntime::log() const
    {
        return impl_->logLines;
    }

    std::vector<std::string> PresentationRuntime::disabledPackages() const
    {
        return impl_->disabledPackages();
    }

    std::size_t PresentationRuntime::packageCount() const
    {
        return impl_->packageCount();
    }

    std::size_t PresentationRuntime::disabledCount() const
    {
        return impl_->disabledCount();
    }
}
