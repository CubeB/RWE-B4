#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <rwe/io/pcx/pcx.h>
#include <rwe/scene/PosterCapture.h>
#include <vector>

/**
 * +MakePoster's sizing rules (#915), from
 * BT-GAME-CONSOLE-COMMANDS-PARITY.md section 6: default 3200x2400, `all` the
 * map's pixel size, each side raised to the visible tile span times sixteen
 * and lowered to the map's, centred on the view and clamped.
 */
namespace rwe
{
    TEST_CASE("a poster side is raised to whole visible tiles", "[poster]")
    {
        CHECK(posterMinimumDimension(0.0f) == 16u);
        CHECK(posterMinimumDimension(1.0f) == 16u);
        CHECK(posterMinimumDimension(16.0f) == 16u);
        CHECK(posterMinimumDimension(16.1f) == 32u);
        CHECK(posterMinimumDimension(1024.0f) == 1024u);
        CHECK(posterMinimumDimension(1025.0f) == 1040u);
        CHECK(posterMinimumDimension(MaxPosterDimension + 16.0f) == MaxPosterDimension);
    }

    TEST_CASE("a poster size is clamped between the view and the map", "[poster]")
    {
        SECTION("the default survives on a large map")
        {
            auto size = clampPosterSize(3200, 2400, 8192, 8192, 1024, 768);
            CHECK(size.width == 3200u);
            CHECK(size.height == 2400u);
        }

        SECTION("a request below the visible span is raised to it")
        {
            auto size = clampPosterSize(100, 100, 8192, 8192, 1024, 768);
            CHECK(size.width == 1024u);
            CHECK(size.height == 768u);
        }

        SECTION("a request beyond the map is lowered to the map")
        {
            auto size = clampPosterSize(20000, 20000, 4096, 3072, 1024, 768);
            CHECK(size.width == 4096u);
            CHECK(size.height == 3072u);
        }

        SECTION("a map smaller than the minimum wins")
        {
            auto size = clampPosterSize(3200, 2400, 512, 384, 1024, 768);
            CHECK(size.width == 512u);
            CHECK(size.height == 384u);
        }

        SECTION("all on a map below the minimum is the map")
        {
            auto size = clampPosterSize(512, 384, 512, 384, 1024, 768);
            CHECK(size.width == 512u);
            CHECK(size.height == 384u);
        }
    }

    TEST_CASE("a poster is centred on the view and moved inside the map", "[poster]")
    {
        CHECK(clampPosterAxis(0.0f, 3200, -4096.0f, 4096.0f) == -1600.0f);
        CHECK(clampPosterAxis(4000.0f, 3200, -4096.0f, 4096.0f) == 896.0f);
        CHECK(clampPosterAxis(-4000.0f, 3200, -4096.0f, 4096.0f) == -4096.0f);
        // A rectangle as wide as the map starts at the map's corner.
        CHECK(clampPosterAxis(0.0f, 8192, -4096.0f, 4096.0f) == -4096.0f);
    }

    TEST_CASE("poster output is bounded", "[poster]")
    {
        CHECK(posterFitsLimits(PosterSize{3200, 2400}));
        CHECK(posterFitsLimits(PosterSize{16384, 16384}));
        CHECK_FALSE(posterFitsLimits(PosterSize{0, 100}));
        CHECK_FALSE(posterFitsLimits(PosterSize{16385, 100}));
        CHECK(posterFitsLimits(PosterSize{16384, 16383}));
    }

    TEST_CASE("the poster keeps the original's BIGSHOT name and RWE's PCX format", "[poster]")
    {
        CHECK(posterFileName() == "BIGSHOT.pcx");
    }

    TEST_CASE("the streaming PCX writer matches the in-memory encoder", "[poster]")
    {
        constexpr unsigned int width = 5;
        constexpr unsigned int height = 3;
        std::vector<std::uint8_t> rgb;
        for (unsigned int y = 0; y < height; ++y)
        {
            for (unsigned int x = 0; x < width; ++x)
            {
                rgb.push_back(static_cast<std::uint8_t>((x * 17) + (y * 3)));
                rgb.push_back(static_cast<std::uint8_t>(255 - (x * 11)));
                rgb.push_back(static_cast<std::uint8_t>(x == y ? 0xFF : 0x00));
            }
        }

        auto directory = std::filesystem::temp_directory_path() / "rwe_poster_writer_test";
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
        auto path = directory / "BIGSHOT.pcx";

        {
            PosterPcxWriter writer(path, width, height);
            REQUIRE(writer.isOpen());
            for (unsigned int y = 0; y < height; ++y)
            {
                REQUIRE(writer.writeRow(rgb.data() + (static_cast<std::size_t>(y) * width * 3)));
            }
            REQUIRE(writer.finish());
        }

        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.is_open());
        std::vector<std::uint8_t> written((std::istreambuf_iterator<char>(in)), (std::istreambuf_iterator<char>()));
        CHECK(written == encodePcx24(width, height, rgb));

        // The header is the one the screenshot path writes, so the file is a
        // 24-bit three-plane PCX rather than the original's 8-bit one.
        REQUIRE(written.size() >= sizeof(PcxHeader));
        const auto* header = reinterpret_cast<const PcxHeader*>(written.data());
        CHECK(header->manufacturer == 10);
        CHECK(header->version == 5);
        CHECK(header->numberOfPlanes == 3);
        CHECK(header->bitsPerPixel == 8);
        CHECK(header->window.xMax == width - 1);
        CHECK(header->window.yMax == height - 1);

        std::filesystem::remove_all(directory, ec);
    }

    TEST_CASE("a poster writer reports a directory it cannot create", "[poster]")
    {
        auto directory = std::filesystem::temp_directory_path() / "rwe_poster_writer_error_test";
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
        std::filesystem::create_directories(directory, ec);
        REQUIRE_FALSE(ec);

        auto blocker = directory / "not-a-directory";
        {
            std::ofstream file(blocker);
            REQUIRE(file.is_open());
            file << "x";
        }

        PosterPcxWriter writer(blocker / "BIGSHOT.pcx", 16, 16);
        CHECK_FALSE(writer.isOpen());

        std::filesystem::remove_all(directory, ec);
    }

    TEST_CASE("a discarded poster leaves no partial file", "[poster]")
    {
        auto directory = std::filesystem::temp_directory_path() / "rwe_poster_writer_partial_test";
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
        auto path = directory / "BIGSHOT.pcx";

        {
            PosterPcxWriter writer(path, 16, 16);
            REQUIRE(writer.isOpen());
            std::vector<std::uint8_t> row(16 * 3, 0x7F);
            REQUIRE(writer.writeRow(row.data()));
            writer.removePartial();
            CHECK_FALSE(std::filesystem::exists(path));
        }

        std::filesystem::remove_all(directory, ec);
    }
}
