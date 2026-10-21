#include "StrategicManager.h"
#include <rwe/sim/SimTicksPerSecond.h>
#include <algorithm>

namespace rwe
{
    namespace
    {
        int countOf(const AiBlackboard& bb, const std::string& unitType)
        {
            auto it = bb.ownedCompletedCounts.find(unitType);
            return it == bb.ownedCompletedCounts.end() ? 0 : it->second;
        }

        /**
         * Whether the value-odds gate is open: our known army metal at least
         * attackValueRatio times the known enemy ground army's to open it, and
         * held open until we fall below retreatValueRatio. The two marks are
         * the hysteresis; a single one would open and shut on consecutive
         * passes as one unit is remembered or forgotten.
         *
         * With nothing of theirs known the gate is shut and the headcount /
         * patience gate is all there is, so an enemy nobody can find cannot
         * stall the AI by keeping this one closed. The same is true when the
         * knob is off, which is the old command stream.
         *
         * RWE-original policy (docs/agents/rwe-ai-adaptability.md S:3); the
         * original's computer player has no value-odds posture, so this is
         * not classic parity.
         */
        bool valueOddsOpen(const AiTuningProfile& profile, AiBlackboard& bb)
        {
            if (!profile.attackUsesValueOdds || bb.knownEnemyArmyMetal <= 0.0f)
            {
                bb.attackOddsCommitted = false;
                return false;
            }

            // The value gate may pull the counted wave forward, but not to a
            // handful of units. Half the counted wave is the floor: a side
            // that happens to out-value one remembered scout still has to
            // have an army, and a profile that has deliberately set
            // attackArmySize out of reach is not overridden by a skirmish.
            // retreatArmySize remains the hard "something worth sending".
            auto halfWave = (profile.attackArmySize + 1) / 2;
            auto needed = std::max(profile.retreatArmySize, halfWave);

            if (!bb.attackOddsCommitted)
            {
                if (bb.armySize >= needed
                    && bb.armyMetal >= profile.attackValueRatio * bb.knownEnemyArmyMetal)
                {
                    bb.attackOddsCommitted = true;
                }
            }
            else if (bb.armyMetal < profile.retreatValueRatio * bb.knownEnemyArmyMetal)
            {
                bb.attackOddsCommitted = false;
            }
            return bb.attackOddsCommitted && bb.armySize >= needed;
        }
    }

    void StrategicManager::update(const AiTuningProfile& profile, AiBlackboard& bb) const
    {
        // Re-evaluated every tick so the hysteresis state is current even
        // during a defend or a wave; only the Boom branch below acts on it.
        auto oddsReady = valueOddsOpen(profile, bb);
        // Something hostile in the base overrides everything else -- except
        // a wave already on its way. Recalling the whole attack for one
        // scout at the extractors is what the army used to do, and it is
        // how a wave that had nearly reached the enemy turned round and
        // walked home; the units gathering at the rally point for the next
        // wave are the ones that answer an intruder (ArmyManager). Once the
        // wave is spent the intruder gets everyone, as before.
        //
        // A big enough push is the exception. The threat memory totals what
        // has actually come at the base lately, and when that is worth more
        // than recallThreatRatio times the wave, home is the fight and the
        // wave comes back. Both halves of the rule need their memories on:
        // with threatMemory off there is no total and this reduces to the
        // old wave-out rule exactly, and proportionalDefence off is the
        // documented control arm for the old response.
        auto waveOut = profile.attackInWaves && profile.reserveAnswersIntruders && bb.phase == GamePhase::Attack && !bb.waveSpent && !bb.attackGroup.empty();
        auto recallWave = false;
        if (waveOut && profile.proportionalDefence && profile.threatMemory && bb.waveMetal > 0.0f && bb.threatTotal > 0)
        {
            recallWave = static_cast<float>(bb.threatTotal) > profile.recallThreatRatio * bb.waveMetal;
        }
        if (!bb.enemiesNearBase.empty() && (!waveOut || recallWave))
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
                // A fleet may call the attack too, when the knob allows it.
                // armySize counts combatUnits and hulls are deliberately kept
                // out of those, so a side whose strength is all afloat never
                // left Boom -- and the phase gates the army ferry, so on an
                // island map the ferry could not run however many ships
                // stood. The borrowed naval scout is subtracted for the same
                // reason armySize subtracts the land one.
                auto haveTarget = bb.enemyBasePosition || !bb.knownEnemies.empty();
                auto fleet = static_cast<int>(bb.navalCombatUnits.size()) - (bb.navalScoutUnitId ? 1 : 0);
                auto fleetReady = profile.attackNavalSize > 0 && fleet >= profile.attackNavalSize;

                // Waited long enough. attackArmySize is a fixed number, and
                // on a map whose economy cannot reach it it is the number
                // that says never attack -- see
                // AiTuningProfile::attackPatienceSeconds for the game that
                // asked for this. Something to send is still required: below
                // retreatArmySize a wave is spent the moment it forms.
                auto waited = profile.attackPatienceSeconds > 0
                    && bb.lastAttackPhase.value > 0
                    && bb.now.value >= bb.lastAttackPhase.value + (static_cast<unsigned int>(profile.attackPatienceSeconds) * SimTicksPerSecond)
                    && bb.armySize >= profile.retreatArmySize;

                // The humanlike opening's first-wave window: at the recorded
                // opening size, from the recorded time. A bias on the count
                // gate rather than a script -- it still needs a target, still
                // needs something worth sending, and holds if the observed
                // enemy army says we are behind (the value gate above).
                auto openingAttackSize = std::max(profile.retreatArmySize, profile.openingAttackArmySize);
                auto openingAttackDue = profile.humanlikeOpening
                    && profile.openingAttackBySeconds >= 0
                    && bb.now.value >= static_cast<unsigned int>(profile.openingAttackBySeconds) * SimTicksPerSecond
                    && bb.armySize >= openingAttackSize
                    && (!profile.attackUsesValueOdds || bb.attackOddsCommitted || bb.knownEnemyArmyMetal <= 0.0f);

                if ((bb.armySize >= profile.attackArmySize || fleetReady || waited || oddsReady || openingAttackDue) && haveTarget)
                {
                    bb.phase = GamePhase::Attack;
                }
                break;
            }
            case GamePhase::Attack:
            {
                // Too few left standing: regroup and rebuild. In waves it is
                // the wave that is counted, not the army -- the units at the
                // rally point are the next wave, not a reason to press on.
                auto spent = profile.attackInWaves ? bb.waveSpent : bb.armySize < profile.retreatArmySize;
                if (spent)
                {
                    bb.phase = GamePhase::Boom;
                    // The next wave earns its own odds; carrying the last
                    // wave's open gate forward would skip the ratio test for
                    // whatever the enemy has rebuilt in the meantime.
                    bb.attackOddsCommitted = false;
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

        // The clock behind attackPatienceSeconds, running from the last
        // tick spent attacking rather than from the start of this build-up:
        // being driven back into Defend over and over must not keep
        // restarting it, which is how the first version of this never fired
        // on the map that asked for it. The first build-up starts it, so an
        // AI that has never attacked is not overdue on tick one.
        if (bb.phase == GamePhase::Attack || (bb.lastAttackPhase.value == 0 && bb.phase == GamePhase::Boom))
        {
            bb.lastAttackPhase = bb.now;
        }

        (void)countOf;
    }
}
