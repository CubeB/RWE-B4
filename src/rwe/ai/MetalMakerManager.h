#pragma once

#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/PlayerId.h>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    /**
     * Switches metal makers on and off so they take the energy that is spare
     * and none that is not.
     *
     * A metal maker is a pump from energy to metal that never stops asking. On
     * TA's numbers one costs sixty energy a second to make one metal, so a
     * base with six solar collectors and two makers is spending its entire
     * generation on them and has nothing left to build with -- which is
     * exactly what the arena found the AI doing: ten minutes in, on a dry map
     * with no fighting at all, it sat at zero energy with a demand of 191
     * against an income of 138 and an army of three, and both makers had been
     * running since the moment they finished.
     *
     * The Wikibooks strategy guide names the same lever from the other side:
     * turn makers on to soak up energy that would otherwise overflow a full
     * store, and turn them off under attack so the plasma batteries can keep
     * firing. Both are the same rule -- makers get what is spare.
     */
    class MetalMakerManager
    {
    public:
        void update(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            std::vector<PlayerCommand>& outCommands);

    private:
        /**
         * What we last decided, so the two water marks can leave a gap in the
         * middle. With one threshold the makers flap on and off every tick
         * while the level sits on it.
         */
        bool makersOn{false};
    };
}
