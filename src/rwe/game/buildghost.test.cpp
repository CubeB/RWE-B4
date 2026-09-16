#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>

namespace rwe
{
    TEST_CASE("localBuildGhostIsActive: a placement ghost stays up until the real order lands", "[buildghost]")
    {
        REQUIRE(localBuildGhostIsActive(LocalBuildGhostKind::Placement, false, GameTime(0), GameTime(1), GameTime(30)));
    }

    TEST_CASE("localBuildGhostIsActive: a placement ghost drops the instant the real order is there, however new it is", "[buildghost]")
    {
        // Drawing both the ghost and the real order at once would double it,
        // so the ghost gives way as soon as there is something to draw instead.
        REQUIRE_FALSE(localBuildGhostIsActive(LocalBuildGhostKind::Placement, true, GameTime(0), GameTime(1), GameTime(30)));
    }

    TEST_CASE("localBuildGhostIsActive: a placement ghost expires if the order never lands", "[buildghost]")
    {
        // A refused or lost command leaves no order to reconcile against, so
        // the clock is what takes the ghost down instead.
        REQUIRE_FALSE(localBuildGhostIsActive(LocalBuildGhostKind::Placement, false, GameTime(0), GameTime(30), GameTime(30)));
    }

    TEST_CASE("localBuildGhostIsActive: a placement ghost survives right up to its timeout", "[buildghost]")
    {
        REQUIRE(localBuildGhostIsActive(LocalBuildGhostKind::Placement, false, GameTime(0), GameTime(29), GameTime(30)));
    }

    TEST_CASE("localBuildGhostIsActive: a cancellation ghost stays up while the order it is hiding is still there", "[buildghost]")
    {
        REQUIRE(localBuildGhostIsActive(LocalBuildGhostKind::Cancellation, true, GameTime(0), GameTime(1), GameTime(30)));
    }

    TEST_CASE("localBuildGhostIsActive: a cancellation ghost drops once the order it was hiding is actually gone", "[buildghost]")
    {
        REQUIRE_FALSE(localBuildGhostIsActive(LocalBuildGhostKind::Cancellation, false, GameTime(0), GameTime(1), GameTime(30)));
    }

    TEST_CASE("localBuildGhostIsActive: a cancellation ghost expires even if the order it was hiding is stuck", "[buildghost]")
    {
        // A build that has already started refuses the cancel outright (see
        // GameScene::cancelBuildOrderAt), so a stuck cancellation ghost has to
        // be able to give up on its own rather than hide the box forever.
        REQUIRE_FALSE(localBuildGhostIsActive(LocalBuildGhostKind::Cancellation, true, GameTime(0), GameTime(30), GameTime(30)));
    }
}
