#include <catch2/catch_test_macros.hpp>
#include <map>
#include <rwe/game/featureplacement.h>

namespace rwe
{
    namespace
    {
        using Placed = std::vector<std::pair<Point, std::string>>;

        // The three shapes from the upstream decoder's test map: a long
        // indestructible strip, a wide destructible slab, and a one-cell
        // indestructible marker. Metal spots and trees on real maps play the
        // same roles.
        const std::map<std::string, FeaturePlacementInfo> kinds{
            {"strip", {8, 1, true}},
            {"slab", {5, 2, false}},
            {"marker", {1, 1, true}},
            {"tree", {1, 1, false}},
            {"metal", {3, 3, true}},
        };

        FeaturePlacementInfo lookup(const std::string& name)
        {
            return kinds.at(name);
        }

        FeaturePlacementResult resolve(const Placed& features, int w = 20, int h = 20)
        {
            return resolveFeatureOverlaps(features, w, h, lookup);
        }
    }

    TEST_CASE("feature placement: features that do not touch all stand", "[featureplacement]")
    {
        Placed in{{{0, 0}, "tree"}, {{5, 5}, "metal"}, {{10, 0}, "slab"}};
        auto out = resolve(in);
        REQUIRE(out.placed == in);
        REQUIRE(out.replaced == 0);
        REQUIRE(out.dropped == 0);
    }

    TEST_CASE("feature placement: a tree drawn over a metal spot is dropped", "[featureplacement]")
    {
        // Show Down, from the upstream audit: lushmetal3 at (5,261) is read
        // before lush05 at (7,262), and the tree is the one that goes.
        Placed in{{{5, 1}, "metal"}, {{7, 2}, "tree"}};
        auto out = resolve(in);
        REQUIRE(out.placed == Placed{{{5, 1}, "metal"}});
        REQUIRE(out.dropped == 1);
        REQUIRE(out.replaced == 0);
    }

    TEST_CASE("feature placement: a later feature replaces an earlier destructible one", "[featureplacement]")
    {
        // The rule RWE did not have: the original erases the older feature
        // and keeps the newcomer, which is what makes the rock fields of
        // Steel Jungle and Lava Alley come out as TA draws them.
        Placed in{{{2, 2}, "slab"}, {{4, 3}, "tree"}};
        auto out = resolve(in);
        REQUIRE(out.placed == Placed{{{4, 3}, "tree"}});
        REQUIRE(out.replaced == 1);
        REQUIRE(out.dropped == 0);
    }

    TEST_CASE("feature placement: a footprint off the map is skipped before it claims anything", "[featureplacement]")
    {
        // Object 3 of the decoder's test: a strip that runs off the right
        // edge is never placed, so the slab that follows over its cells is.
        Placed in{{{15, 0}, "strip"}, {{14, 0}, "slab"}};
        auto out = resolve(in);
        REQUIRE(out.placed == Placed{{{14, 0}, "slab"}});
        REQUIRE(out.dropped == 1);
        REQUIRE(out.replaced == 0);
    }

    TEST_CASE("feature placement: the last attribute row and column are not part of the map", "[featureplacement]")
    {
        // Show Down's grid is 322x136 attributes, so 321x135 cells; 21 of
        // its trees sit on x=321 or y=135 and TA never places them.
        Placed in{{{321, 1}, "tree"}, {{8, 135}, "tree"}, {{320, 134}, "tree"}};
        auto out = resolve(in, 321, 135);
        REQUIRE(out.placed == Placed{{{320, 134}, "tree"}});
        REQUIRE(out.dropped == 2);
    }

    TEST_CASE("feature placement: a newcomer stopped by an indestructible keeps what it already displaced", "[featureplacement]")
    {
        // A slab that has erased a tree on its first cell and then meets a
        // marker on a later one is dropped, and the tree does not come back.
        // That is the decoded behaviour, kept rather than tidied.
        Placed in{{{0, 0}, "tree"}, {{3, 0}, "marker"}, {{0, 0}, "slab"}};
        auto out = resolve(in);
        REQUIRE(out.placed == Placed{{{3, 0}, "marker"}});
        REQUIRE(out.replaced == 1);
        REQUIRE(out.dropped == 1);
    }

    TEST_CASE("feature placement: survivors keep their original order", "[featureplacement]")
    {
        Placed in{{{0, 0}, "tree"}, {{2, 0}, "tree"}, {{2, 0}, "marker"}, {{6, 6}, "tree"}};
        auto out = resolve(in);
        REQUIRE(out.placed == Placed{{{0, 0}, "tree"}, {{2, 0}, "marker"}, {{6, 6}, "tree"}});
    }
}
