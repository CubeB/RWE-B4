#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <rwe/ai/AiController.h>
#include <string>
#include <vector>

namespace rwe
{
    struct ModAiScript;

    /**
     * MOD-09, docs/MODDING-SDK-FINDINGS.md MOD-SDK-010. Budgets for one
     * scripted seat. They are per call, not per second, so frame rate and
     * machine speed cannot change what a script decides.
     */
    struct ModAiLimits
    {
        std::uint64_t instructionBudgetPerCall = 200000;
        std::size_t memoryBudget = 4 * 1024 * 1024;
        std::size_t maxCommandsPerTick = 1024;
        std::size_t maxLogEntries = 1000;
        std::size_t maxLogLength = 1024;
    };

    /**
     * One computer seat played by a package's sandboxed Lua script.
     *
     * It runs inside the deterministic simulation, on the same per-tick AI
     * pass as every other engine, and talks to the simulation only through
     * the ordinary PlayerCommand stream. Its Lua state is persistent across
     * ticks; its randomness is its own minstd_rand seeded from the resolved
     * set and the seat's single simulation-RNG draw. No wall clock, frame
     * rate, ping or presentation state reaches it.
     *
     * The observation surface is deliberately narrow: ai.my_units() returns
     * the seat's own units only, so a script gets no free enemy omniscience.
     * A script that faults (Lua error, budget exhaustion, an unknown or
     * foreign unit) disables its seat for the rest of the match and emits
     * nothing for that tick, exactly as ModRuntime disables a faulty package.
     *
     * Not yet implemented: the private Lua heap, plan and RNG position are not
     * part of the save or rejoin contract, so save/resume of a Lua seat is not
     * claimed. The save header records the engine name only.
     */
    class ModAiController final : public AiController
    {
    public:
        ModAiController(
            PlayerId playerId,
            std::shared_ptr<const ModAiScript> script,
            AiTuningProfile profile,
            std::uint64_t rngSeed,
            ModAiLimits limits = {});
        ~ModAiController() override;

        ModAiController(const ModAiController&) = delete;
        ModAiController& operator=(const ModAiController&) = delete;

        void tick(const GameSimulation& sim, std::vector<PlayerCommand>& outCommands) override;

        PlayerId getPlayerId() const override { return playerId_; }

        const AiTuningProfile& getProfile() const override { return profile_; }

        AiEngine engine() const override { return AiEngine::Lua; }

        /** True once a fault has disabled the seat. A disabled seat never thinks again. */
        bool disabled() const;

        /** The fault that disabled the seat, or empty while it is healthy. */
        const std::string& lastError() const;

        /** The script's ai.log output and the controller's own fault lines. */
        const std::vector<std::string>& log() const;

        /** The controller's private random stream, for diagnostics and a future save. */
        std::string rngState() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
        PlayerId playerId_;
        AiTuningProfile profile_;
    };
}
