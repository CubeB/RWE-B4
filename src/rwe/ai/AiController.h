#pragma once

#include <rwe/ai/AiTuningProfile.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/PlayerId.h>
#include <vector>

namespace rwe
{
    struct GameSimulation;
    class AiPlayerController;

    /** Which computer player a seat plays: see ADR-0005. */
    enum class AiEngine
    {
        /** The original's: ClassicAiController, written from TOTALA-EXE-COMPUTER-PLAYER.md. The default. */
        Classic,
        /** RWE's own, improved one: AiPlayerController. */
        Rwe,
    };

    /** "classic" or "rwe", as --ai-engine and the save file spell it. */
    const char* aiEngineName(AiEngine engine);

    /** The inverse of aiEngineName, ignoring case; nothing for a name it does not know. */
    bool aiEngineFromName(const char* name, AiEngine& out);

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
    };
}
