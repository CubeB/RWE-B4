#include "ArchiveBackend.h"

#include <archive.h>
#include <archive_entry.h>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace rwe
{
    namespace
    {
        struct TempDirectory
        {
            fs::path path = fs::temp_directory_path() / ("rwe-archive-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + "-" + std::to_string(std::random_device{}()));
            TempDirectory() { fs::create_directories(path); }
            ~TempDirectory() { std::error_code ec; fs::remove_all(path, ec); }
        };

        void writeArchive(const fs::path& path, bool sevenZip)
        {
            auto* writer = archive_write_new();
            REQUIRE(writer != nullptr);
            REQUIRE(archive_write_set_format_by_name(writer, sevenZip ? "7zip" : "zip") == ARCHIVE_OK);
            REQUIRE(archive_write_add_filter_none(writer) == ARCHIVE_OK);
#ifdef _WIN32
            REQUIRE(archive_write_open_filename_w(writer, path.c_str()) == ARCHIVE_OK);
#else
            REQUIRE(archive_write_open_filename(writer, path.c_str()) == ARCHIVE_OK);
#endif
            auto* entry = archive_entry_new();
            REQUIRE(entry != nullptr);
            archive_entry_set_pathname_utf8(entry, "payload/data.bin");
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
            constexpr std::string_view bytes = "archive-backend-exact-bytes\0with-nul";
            archive_entry_set_size(entry, bytes.size());
            REQUIRE(archive_write_header(writer, entry) == ARCHIVE_OK);
            REQUIRE(archive_write_data(writer, bytes.data(), bytes.size()) == static_cast<la_ssize_t>(bytes.size()));
            archive_entry_free(entry);
            REQUIRE(archive_write_close(writer) == ARCHIVE_OK);
            REQUIRE(archive_write_free(writer) == ARCHIVE_OK);
        }
    }

    TEST_CASE("bundled archive backend inventories and extracts ZIP and 7z bytes", "[setup][archive]")
    {
        TempDirectory temp;
        constexpr std::string_view expected = "archive-backend-exact-bytes\0with-nul";
        for (const auto sevenZip : {false, true})
        {
            const auto archivePath = temp.path / (sevenZip ? "sample.7z" : "sample.zip");
            const auto staging = temp.path / (sevenZip ? "extract-7z" : "extract-zip");
            writeArchive(archivePath, sevenZip);
            const auto inventory = inspectArchive(archivePath);
            REQUIRE(inventory.entries.size() == 1);
            CHECK(inventory.entries[0].path == "payload/data.bin");
            CHECK(inventory.entries[0].size == expected.size());
            CHECK(inventory.totalBytes == expected.size());
            const auto extracted = extractArchive(archivePath, staging);
            CHECK(extracted.entries.size() == 1);
            std::ifstream input(staging / "payload/data.bin", std::ios::binary);
            const std::string actual((std::istreambuf_iterator<char>(input)), {});
            CHECK(actual == expected);
        }
    }

    TEST_CASE("archive extraction refuses reused staging and cleans owned staging on cancellation", "[setup][archive]")
    {
        TempDirectory temp;
        const auto archivePath = temp.path / "sample.zip";
        writeArchive(archivePath, false);
        const auto existing = temp.path / "existing";
        fs::create_directory(existing);
        std::ofstream(existing / "keep.txt") << "keep";
        CHECK_THROWS(extractArchive(archivePath, existing));
        CHECK(fs::exists(existing / "keep.txt"));

        const auto cancelled = temp.path / "cancelled";
        CHECK_THROWS(extractArchive(archivePath, cancelled, [](std::uint64_t, std::uint64_t) { return false; }));
        CHECK_FALSE(fs::exists(cancelled));
    }

    TEST_CASE("bundled archive backend extracts the licensed RAR5 fixture exactly", "[setup][archive]")
    {
        TempDirectory temp;
        const auto archivePath = fs::path(RWE_SOURCE_DIR) / "tests/fixtures/archive-backend/rar5-compressed.rar";
        const auto inventory = inspectArchive(archivePath);
        REQUIRE(inventory.entries.size() == 1);
        CHECK(inventory.entries[0].path == "test.bin");
        CHECK(inventory.entries[0].size == 1200);
        const auto staging = temp.path / "rar5";
        extractArchive(archivePath, staging);
        std::ifstream input(staging / "test.bin", std::ios::binary);
        const std::string actual((std::istreambuf_iterator<char>(input)), {});
        std::string expected;
        for (std::int32_t i = 0; i < 300; ++i)
        {
            const auto k = i + 1;
            auto value = k * k - 3 * k + 1;
            if (value < 0) value = 0;
            for (int byte = 0; byte < 4; ++byte)
                expected.push_back(static_cast<char>((static_cast<std::uint32_t>(value) >> (byte * 8)) & 0xff));
        }
        CHECK(actual == expected);
    }
}
