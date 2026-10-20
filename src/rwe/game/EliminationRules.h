#pragma once

namespace rwe
{
    enum class EliminationFlow { End, AskWatch, ForcedWatch };

    // Corrected BT-89; docs/agents/383-699-700-findings.md. Hosted
    // computers force watching; without them an eligible human gets a choice.
    constexpr EliminationFlow eliminationFlow(bool multiplayer, bool permitted, bool hostedComputers, bool removed)
    {
        return !multiplayer || removed || (!permitted && !hostedComputers) ? EliminationFlow::End
            : hostedComputers ? EliminationFlow::ForcedWatch : EliminationFlow::AskWatch;
    }
}
