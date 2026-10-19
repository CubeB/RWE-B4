#include <catch2/catch_test_macros.hpp>
#include <rwe/ui/UiPanorama.h>

namespace rwe
{
    TEST_CASE("Briefing panorama uses strict wall-clock deadlines without catch-up", "[panorama][campaign]")
    {
        PanoramaScrollState state;
        state.advance(0, 0, 2440);
        REQUIRE(state.offset == 0);
        state.advance(1, 1, 2440);
        REQUIRE(state.offset == 1);
        state.advance(3, 3, 2440); // Equality is not due.
        REQUIRE(state.offset == 1);
        state.advance(4, 5, 2440); // Deadline read can cross a clock tick.
        REQUIRE(state.offset == 2);
        REQUIRE(state.deadline == 7);
        state.advance(600, 600, 2440); // One late call: one pixel, not 200.
        REQUIRE(state.offset == 3);
        REQUIRE(state.deadline == 602);
        state.offset = 2439;
        state.advance(603, 603, 2440);
        REQUIRE(state.offset == 0);
    }

    TEST_CASE("Shipped GreenPan and LavaPan use their short final strip in the ring", "[panorama][campaign]")
    {
        // totala1.hpi, ccdata.ccx, btdata.ccx: 640,640,640,520; height122.
        // BT-267, docs/agents/factory-death-and-briefing.md.
        const std::vector<unsigned int> widths{640, 640, 640, 520};
        REQUIRE(panoramaTileOffsets(widths, 0) == std::vector<int>{0,640,1280,1920,2440});
        REQUIRE(panoramaTileOffsets(widths, 640) == std::vector<int>{-640,0,640,1280,1800});
        // Last pixel before wrap: extra frame zero begins one pixel right.
        REQUIRE(panoramaTileOffsets(widths, 2439) == std::vector<int>{-2439,-1799,-1159,-519,1});
        PanoramaScrollState persistent{2439, 100};
        persistent.advance(100, 100, 2440); // Briefing reentry does not reset it.
        REQUIRE(persistent.offset == 2439);
        persistent.advance(101, 101, 2440);
        REQUIRE(persistent.offset == 0);
    }
}
