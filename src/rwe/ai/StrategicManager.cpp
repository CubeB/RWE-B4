#include "StrategicManager.h"

namespace rwe
{
    namespace
    {
        int countOf(const AiBlackboard& bb, const std::string& unitType)
        {
            auto it = bb.ownedCompletedCounts.find(unitType);
            return it == bb.ownedCompletedCounts.end() ? 0 : it->second;
        }
    }

    void StrategicManager::update(const AiTuningProfile& profile, AiBlackboard& bb) const
    {
        // Something hostile in the base overrides everything else.
        if (!bb.enemiesNearBase.empty())
        {
            bb.phase = GamePhase::Defend;
            return;
        }

        switch (bb.phase)
        {
            case GamePhase::Opening:
            {
                // The opening is over once a factory stands.
                if (!bb.factories.empty())
                {
                    bb.phase = GamePhase::Boom;
                }
                break;
            }
            case GamePhase::Boom:
            {
                if (bb.armySize >= profile.attackArmySize && (bb.enemyBasePosition || !bb.knownEnemies.empty()))
                {
                    bb.phase = GamePhase::Attack;
                }
                break;
            }
            case GamePhase::Attack:
            {
                // Too few left standing: regroup and rebuild.
                if (bb.armySize < profile.retreatArmySize)
                {
                    bb.phase = GamePhase::Boom;
                }
                break;
            }
            case GamePhase::Defend:
            {
                // Threat gone (handled above when present): go back to what we were doing.
                bb.phase = bb.factories.empty() ? GamePhase::Opening : GamePhase::Boom;
                break;
            }
            case GamePhase::Tech:
            case GamePhase::Endgame:
                break;
        }

        (void)countOf;
    }
}
