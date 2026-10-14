#pragma once

namespace rwe
{
    /**
     * Which computer player a seat plays: see ADR-0005.
     *
     * Kept in a header of its own because the game's parameters, the save
     * file and the personalities all name an engine, and none of them should
     * have to include the controller interface (and with it PlayerCommand.h)
     * to do it.
     */
    enum class AiEngine
    {
        /** The original's: ClassicAiController, written from TOTALA-EXE-COMPUTER-PLAYER.md. The default. */
        Classic,
        /** RWE's own, improved one: AiPlayerController. */
        Rwe,
    };

    /** "classic" or "rwe", as --ai-engine and the save file spell it. */
    const char* aiEngineName(AiEngine engine);

    /** The inverse of aiEngineName, ignoring case; false for a name it does not know. */
    bool aiEngineFromName(const char* name, AiEngine& out);
}
