#pragma once

#include <ranges>
#include <utility>

namespace rwe
{
    /**
     * The values of a range's elements that `c` maps to an engaged optional,
     * as a lazy view.
     *
     * The range is forwarded, not copied: an lvalue container comes through
     * as a ref_view of the caller's own object and an rvalue as an
     * owning_view. Taking it by value made the view refer to the parameter,
     * which was gone by the time anyone iterated -- every skirmish on Linux
     * faulted at its first tick in the network service's scene-time estimate.
     */
    template <typename Range, typename Chooser>
    auto choose(Range&& r, Chooser c)
    {
        return std::forward<Range>(r)
            | std::views::transform(c)
            | std::views::filter([](const auto& e) { return e.has_value(); })
            | std::views::transform([](const auto& e) { return *e; });
    }
}
