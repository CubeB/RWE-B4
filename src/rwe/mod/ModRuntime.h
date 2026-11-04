#pragma once

#include <cstdint>
#include <memory>
#include <optional>
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
        // Across all packages, between two applications.
        std::size_t maxPendingEffects = 10000;
    };

    // Whether `unit` exists in the simulation `context` points at.
    using ModRuntimeUnitExists = bool (*)(const void* context, UnitId unit);

    // What rwe.unit_info reports (MOD-SDK-018). Runtime rules run on every
    // peer over the whole simulation, so nothing here is filtered by sight.
    struct ModUnitInfo
    {
        PlayerId owner{0};
        // The FBI name, never a definition index.
        std::string unitType;
        unsigned int hitPoints = 0;
        unsigned int maxHitPoints = 0;
        bool complete = true;
        bool dead = false;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // Fills `info` and returns true when `unit` exists.
    using ModRuntimeUnitInfo = bool (*)(const void* context, UnitId unit, ModUnitInfo& info);

    // The simulation as a runtime call sees it. Plain function pointers and
    // a context, bound before every dispatch (see bind).
    struct ModRuntimeWorld
    {
        const void* context = nullptr;
        ModRuntimeUnitExists unitExists = nullptr;
        ModRuntimeUnitInfo unitInfo = nullptr;
    };

    // A change a runtime script asks the simulation to make (MOD-SDK-020).
    // Queued during a call and applied by the simulation at one fixed
    // point, the end of the tick, in the order queued.
    struct ModEffect
    {
        enum class Kind
        {
            // Restore hit points to a finished, living unit, capped at its
            // maximum.
            Heal,
        };
        Kind kind = Kind::Heal;
        std::string packageId;
        UnitId unit{0};
        unsigned int amount = 0;
    };

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
        void bind(unsigned int tick, const ModRuntimeWorld& world);
        // As above with no unit_info source: unit_info reports nil.
        void bind(unsigned int tick, const void* context, ModRuntimeUnitExists unitExists);

        // Unit lifecycle events (MOD-SDK-017), in load order, to packages that
        // export them. A fault disables the package, as for on_tick.
        //   on_unit_created(unit, owner, unitType)  unitType is the FBI name
        //   on_unit_completed(unit)
        //   on_unit_killed(unit, attacker)  attacker is nil when there is none
        void onUnitCreated(UnitId unit, PlayerId owner, const std::string& unitType);
        void onUnitCompleted(UnitId unit);
        void onUnitKilled(UnitId unit, std::optional<UnitId> attacker);

        // A fold over packages that export hook_damage_v1:
        //   hook_damage_v1(victim, attacker, amount, paralyzer, prev) -> number
        // A fault disables that package and passes prev through unchanged.
        float adjustDamage(UnitId victim, UnitId attacker, float amount, bool paralyzer);

        // The effects queued since the last call, in order, leaving the
        // queue empty. The simulation applies them after on_tick.
        std::vector<ModEffect> takeEffects();

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
