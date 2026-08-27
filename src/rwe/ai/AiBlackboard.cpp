#include "AiBlackboard.h"

namespace rwe
{
    const char* gamePhaseName(GamePhase phase)
    {
        switch (phase)
        {
            case GamePhase::Opening: return "Opening";
            case GamePhase::Boom: return "Boom";
            case GamePhase::Attack: return "Attack";
            case GamePhase::Defend: return "Defend";
            case GamePhase::Tech: return "Tech";
            case GamePhase::Endgame: return "Endgame";
        }
        return "?";
    }
}
