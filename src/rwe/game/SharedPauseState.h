#pragma once

#include <cstdint>
#include <tuple>

namespace rwe
{
    /** One shared pause, ordered independently of simulation ticks. */
    struct SharedPauseState
    {
        std::uint64_t revision{0};
        unsigned int player{0};
        bool paused{false};

        bool merge(const SharedPauseState& incoming)
        {
            if (incoming.revision == 0 || std::tie(incoming.revision, incoming.player) <= std::tie(revision, player))
            {
                return false;
            }
            *this = incoming;
            return true;
        }
    };
}
