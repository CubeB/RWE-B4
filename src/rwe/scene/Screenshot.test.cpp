#include <catch2/catch_test_macros.hpp>
#include <rwe/scene/Screenshot.h>

namespace rwe
{
    TEST_CASE("screenshots are numbered one past the highest already taken", "[screenshot]")
    {
        // The first screenshot in an empty folder is SHOT0001 (BT-161), and a
        // file numbered 0000 is ignored.
        CHECK(nextScreenshotNumber({}) == 1u);
        CHECK(nextScreenshotNumber({"SHOT0000.pcx"}) == 1u);
        CHECK(nextScreenshotNumber({"SHOT0003.pcx", "SHOT0001.pcx"}) == 4u);
        CHECK(nextScreenshotNumber({"shot0009.PCX"}) == 10u);
        CHECK(nextScreenshotNumber({"SHOTabcd.pcx", "SHOT0005.png", "notes.txt"}) == 1u);
    }

    TEST_CASE("any digits after SHOT are the number, and past 9999 it goes on rising", "[screenshot]")
    {
        CHECK(nextScreenshotNumber({"SHOT12.pcx"}) == 13u);
        CHECK(nextScreenshotNumber({"SHOT12345.pcx", "SHOT0007.pcx"}) == 12346u);
        CHECK(nextScreenshotNumber({"SHOT9999.pcx"}) == 10000u);
        CHECK(nextScreenshotNumber({"SHOT10000.pcx"}) == 10001u);
        CHECK(screenshotFileName(10001) == "SHOT10001.pcx");
    }

    TEST_CASE("screenshot file names are the original's", "[screenshot]")
    {
        CHECK(screenshotFileName(0) == "SHOT0000.pcx");
        CHECK(screenshotFileName(42) == "SHOT0042.pcx");
        CHECK(screenshotFileName(1234) == "SHOT1234.pcx");
    }
}
