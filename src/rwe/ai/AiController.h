#pragma once

#include <rwe/ai/AiEngine.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/game/PlayerCommand.h>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <rwe/sim/PlayerId.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace rwe
{
    struct GameSimulation;
    class AiPlayerController;

    /**
     * One computer player, whichever engine plays it. It runs inside the
     * deterministic simulation and only ever talks to it through the same
     * PlayerCommand stream a human uses.
     *
     * The destructor is defined out of line, in AiController.cpp, for the
     * reason AiPlayerController's is: GameSimulation owns these through a
     * unique_ptr and an implicit destructor would be generated in
     * GameSimulation.cpp, whose section budget cannot afford it.
     */
    class AiController
    {
    public:
        virtual ~AiController();

        /** One simulation tick of thinking. Commands are appended to `outCommands`. */
        virtual void tick(const GameSimulation& sim, std::vector<PlayerCommand>& outCommands) = 0;

        virtual PlayerId getPlayerId() const = 0;

        /**
         * The difficulty, the idle switch and the income multiplier. The
         * simulation reads `resourceCheatMultiplier` from here when it settles
         * the player's economy, so every engine has to fill it in: for the
         * classic one it is the original's handicap, 0.5 / 0.7 / 1.0.
         */
        virtual const AiTuningProfile& getProfile() const = 0;

        virtual AiEngine engine() const = 0;

        /**
         * The RWE controller, for the code that reads its blackboard (the
         * arena report, the debug panel); null for any other engine.
         */
        virtual const AiPlayerController* asRweController() const { return nullptr; }

        /**
         * State that decides this controller's future commands and has to
         * survive a save (MOD-SDK-021). Null for an engine that rebuilds on
         * load, which the native engines do by design
         * (docs/ai-architecture-proposal.md, placement B).
         */
        virtual nlohmann::json saveState() const;

        /** Restores what saveState wrote; throws std::runtime_error if it cannot. */
        virtual void loadState(const nlohmann::json& state);

        /** A digest of saveState for the sync hash, or empty when there is none. */
        virtual std::string stateHash() const;
    };

    using AiControllerMap = std::unordered_map<PlayerId, std::unique_ptr<AiController>>;

    /**
     * MOD-SDK-021. Every controller's saveState, as [{player, state}] in
     * player order, or null when none keeps any -- so a game with only
     * native engines saves exactly what it did before.
     */
    nlohmann::json aiControllerStates(const AiControllerMap& controllers);

    /** Restores what aiControllerStates wrote, refusing a seat that is missing. */
    void loadAiControllerStates(AiControllerMap& controllers, const nlohmann::json& states);

    /** A digest of every controller's stateHash in player order; empty when none keeps state. */
    std::string aiControllerStatesHash(const AiControllerMap& controllers);
}
