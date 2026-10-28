#include "WeaponDefinition.h"

namespace rwe
{
    bool communityPatchSuppressesFire(const WeaponDefinition& definition, bool firerOverWater)
    {
        // TADR's notoverwater/notoverland: the shot is held, not cancelled --
        // the target and the reload are kept, and no ammunition is spent. The
        // flags only mean anything under the Community Patch ruleset, which
        // the caller checks.
        if (definition.notOverWater && firerOverWater)
        {
            return true;
        }
        if (definition.notOverLand && !firerOverWater)
        {
            return true;
        }
        return false;
    }
}
