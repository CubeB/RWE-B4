#include <catch2/catch_test_macros.hpp>
#include <rwe/scene/Screenshot.h>

namespace rwe
{
    TEST_CASE("screenshots are numbered one past the highest already taken", "[screenshot]")
    {
        CHECK(nextScreenshotNumber({}) == 0u);
        CHECK(nextScreenshotNumber({"SHOT0000.pcx"}) == 1u);
        CHECK(nextScreenshotNumber({"SHOT0003.pcx", "SHOT0001.pcx"}) == 4u);
        CHECK(nextScreenshotNumber({"shot0009.PCX"}) == 10u);
        CHECK(nextScreenshotNumber({"SHOT12.pcx", "SHOTabcd.pcx", "SHOT0005.png", "notes.txt", "SHOT00050.pcx"}) == 0u);
    }

    TEST_CASE("screenshot file names are the original's", "[screenshot]")
    {
        CHECK(screenshotFileName(0) == "SHOT0000.pcx");
        CHECK(screenshotFileName(42) == "SHOT0042.pcx");
        CHECK(screenshotFileName(1234) == "SHOT1234.pcx");
    }
}
