#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <rwe/presentation/PresentationDrawList.h>
#include <rwe/presentation/PresentationScript.h>
#include <rwe/presentation/PresentationView.h>
#include <rwe/sim/FeatureId.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * MOD-10, docs/MODDING-SDK-FINDINGS.md MOD-SDK-013.
     *
     * The presentation half of the mod SDK: one sandboxed Lua state per
     * presentation package, running outside the simulation. It never sees a
     * `GameSimulation&`, never draws from `GameSimulation::rng`, and never
     * writes the simulation. What it may do is ask the filtered `PlayerView`
     * questions, emit draw commands, and submit ordinary player commands
     * through a sink the scene validates.
     *
     * RWE-original. The original has no presentation scripts.
     */
    constexpr int currentPresentationApiVersion = 1;

    struct PresentationRuntimeLimits
    {
        std::uint64_t instructionBudgetPerCall = 200000;
        std::size_t memoryBudgetPerPackage = 8 * 1024 * 1024;
        std::size_t maxLogEntries = 1000;
        std::size_t maxLogLength = 1024;
    };

    /** What the scene knows this frame, passed beside the filtered view. */
    struct PresentationFrameInput
    {
        float uiWidth{640.0f};
        float uiHeight{480.0f};
        float deltaSeconds{0.0f};
        float mouseX{0.0f};
        float mouseY{0.0f};
        bool mouseInsideWorld{false};

        float cameraX{0.0f};
        float cameraY{0.0f};
        float cameraZ{0.0f};
        float cameraZoom{1.0f};

        /** The units the local player has selected, as the native UI holds them. */
        std::vector<UnitId> selectedUnits;

        std::optional<UnitId> hoveredUnit;
        std::optional<FeatureId> hoveredFeature;
    };

    /**
     * A command a presentation mod asked for. The scene maps these onto the
     * ordinary per-tick player command buffer; the runtime never applies one
     * itself, so a mod cannot invent an order the native interface could not
     * have issued.
     */
    struct PresentationCommand
    {
        enum class Kind
        {
            Move,
            Stop,
            Attack,
        };

        Kind kind{Kind::Stop};
        std::vector<UnitId> units;
        SimVector position;
        UnitId target;
    };

    class PresentationRuntime
    {
    public:
        explicit PresentationRuntime(
            std::vector<PresentationPackage> packages,
            PresentationRuntimeLimits limits = {});
        ~PresentationRuntime();

        PresentationRuntime(const PresentationRuntime&) = delete;
        PresentationRuntime& operator=(const PresentationRuntime&) = delete;

        bool hasPackages() const;

        /**
         * Runs every enabled package's `on_frame` and replaces the draw list.
         * `view` is borrowed for the duration of the call only.
         */
        void beginFrame(const PlayerView& view, const PresentationFrameInput& input);

        const PresentationDrawList& drawList() const;

        /** Empties the commands collected since the last call. */
        std::vector<PresentationCommand> takeCommands();

        /** True when an enabled package consumed the event. */
        bool mouseDown(const PlayerView& view, const PresentationFrameInput& input, int button);
        bool mouseUp(const PlayerView& view, const PresentationFrameInput& input, int button);
        void mouseMove(const PlayerView& view, const PresentationFrameInput& input);
        bool keyDown(const PlayerView& view, const PresentationFrameInput& input, int key);

        /**
         * Rebuilds every package from `packages`. The current runtime stays
         * alive and keeps drawing when the replacement fails to load, so a
         * half-saved script does not take the HUD down with it.
         */
        bool reload(std::vector<PresentationPackage> packages);

        const std::vector<std::string>& log() const;
        std::vector<std::string> disabledPackages() const;
        std::size_t packageCount() const;
        std::size_t disabledCount() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
        PresentationRuntimeLimits limits_;
    };
}
