#pragma once

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <rwe/mod/ModStateStore.h>
#include <string>
#include <vector>

namespace rwe
{
    // MOD-08, docs/MODDING-SDK-FINDINGS.md. A persistent runtime script: one
    // sandboxed Lua state per package, alive across ticks, folding damage and
    // receiving tick events. RWE-original.
    constexpr int currentModRuntimeApiVersion = 1;

    struct ModRuntimePackage
    {
        std::string id;
        std::string scriptName;
        std::string source;
        nlohmann::json options = nlohmann::json::object();
    };

    struct ModRuntimeLimits
    {
        std::uint64_t instructionBudgetPerCall = 200000;
        std::size_t memoryBudgetPerPackage = 4 * 1024 * 1024;
        std::size_t maxLogEntries = 1000;
        std::size_t maxLogLength = 1024;
    };

    // Whether `unit` exists in the simulation `context` points at.
    using ModRuntimeUnitExists = bool (*)(const void* context, UnitId unit);

    class ModRuntime
    {
    public:
        // The store is owned by the caller (GameSimulation) and must outlive
        // this runtime.
        ModRuntime(
            std::vector<ModRuntimePackage> packages,
            std::uint32_t seed,
            ModStateStore& store,
            ModRuntimeLimits limits = {});
        ~ModRuntime();

        ModRuntime(const ModRuntime&) = delete;
        ModRuntime& operator=(const ModRuntime&) = delete;

        // Runs on_tick(tick) for every enabled package, in load order. Also
        // sets the tick rwe.tick() reports.
        void onTick(unsigned int tick);

        // Binds what the calls that follow see: the tick rwe.tick() reports,
        // and the check behind rwe.unit_set's refusal of a unit that does
        // not exist (MOD-SDK-016). The simulation binds itself before every
        // dispatch rather than once, because a GameSimulation is moved after
        // its runtime is attached and a stored pointer to it would dangle.
        // Never bound (isolated tests): tick 0, and every unit id accepted.
        void bind(unsigned int tick, const void* context, ModRuntimeUnitExists unitExists);

        // A fold over packages that export hook_damage_v1:
        //   hook_damage_v1(victim, attacker, amount, paralyzer, prev) -> number
        // A fault disables that package and passes prev through unchanged.
        float adjustDamage(UnitId victim, UnitId attacker, float amount, bool paralyzer);

        ModStateStore& store();
        const ModStateStore& store() const;

        std::vector<std::string> disabledPackages() const;
        const std::vector<std::string>& log() const;

        // Runtime metadata: disabled packages, per-package RNG positions and
        // the store. Canonical JSON, plus a SHA-256 over it.
        nlohmann::json toJson() const;
        void loadJson(const nlohmann::json& value);
        std::string hash() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
