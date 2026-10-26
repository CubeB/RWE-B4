#include "ArchiveBackend.h"

#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <clocale>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <locale.h>
#endif

namespace fs = std::filesystem;

namespace rwe
{
    namespace
    {
        using namespace std::string_view_literals;

        constexpr auto ArchiveBytes = "archive-backend-exact-bytes\0with-nul"sv;

        struct TempDirectory
        {
            fs::path path = fs::temp_directory_path() / ("rwe-archive-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + "-" + std::to_string(std::random_device{}()));
            TempDirectory() { fs::create_directories(path); }
            ~TempDirectory() { std::error_code ec; fs::remove_all(path, ec); }
        };

        struct CurrentPathGuard
        {
            fs::path original = fs::current_path();
            ~CurrentPathGuard()
            {
                std::error_code error;
                fs::current_path(original, error);
            }
        };

        struct EnvironmentVariableGuard
        {
            std::string name;
            std::optional<std::string> original;

            explicit EnvironmentVariableGuard(std::string variable) : name(std::move(variable))
            {
                if (const auto* value = std::getenv(name.c_str())) original = value;
            }

            ~EnvironmentVariableGuard() { set(original); }

            void clear() { set(std::nullopt); }

        private:
            void set(const std::optional<std::string>& value)
            {
#ifdef _WIN32
                _putenv_s(name.c_str(), value ? value->c_str() : "");
#else
                if (value)
                    setenv(name.c_str(), value->c_str(), 1);
                else
                    unsetenv(name.c_str());
#endif
            }
        };

        void writeArchiveEntries(const fs::path& path, bool sevenZip,
            const std::vector<std::pair<std::string, bool>>& entries)
        {
            auto* writer = archive_write_new();
            REQUIRE(writer != nullptr);
            REQUIRE(archive_write_set_format_by_name(writer, sevenZip ? "7zip" : "zip") == ARCHIVE_OK);
            if (sevenZip)
                REQUIRE(archive_write_set_format_option(writer, "7zip", "compression", "lzma2") == ARCHIVE_OK);
            REQUIRE(archive_write_add_filter_none(writer) == ARCHIVE_OK);
#ifdef _WIN32
            REQUIRE(archive_write_open_filename_w(writer, path.c_str()) == ARCHIVE_OK);
#else
            REQUIRE(archive_write_open_filename(writer, path.c_str()) == ARCHIVE_OK);
#endif
            for (const auto& [entryPath, symlink] : entries)
            {
                auto* entry = archive_entry_new();
                REQUIRE(entry != nullptr);
                archive_entry_set_pathname_utf8(entry, entryPath.c_str());
                archive_entry_set_filetype(entry, symlink ? AE_IFLNK : AE_IFREG);
                if (symlink) archive_entry_set_symlink(entry, "target");
                archive_entry_set_perm(entry, 0644);
                archive_entry_set_size(entry, symlink ? 0 : ArchiveBytes.size());
                REQUIRE(archive_write_header(writer, entry) == ARCHIVE_OK);
                if (!symlink)
                    REQUIRE(archive_write_data(writer, ArchiveBytes.data(), ArchiveBytes.size()) == static_cast<la_ssize_t>(ArchiveBytes.size()));
                archive_entry_free(entry);
            }
            REQUIRE(archive_write_close(writer) == ARCHIVE_OK);
            REQUIRE(archive_write_free(writer) == ARCHIVE_OK);
        }

        void writeArchive(const fs::path& path, bool sevenZip, const char* entryPath = "payload/data.bin",
            bool symlink = false)
        {
            writeArchiveEntries(path, sevenZip, {{entryPath, symlink}});
        }

        // PACKAGE-007 covers this RWE-original archive boundary. Write ZIP records
        // directly when a fixture needs UTF-8 names. libarchive's
        // ZIP writer converts pathname_utf8 through the active locale on some
        // platforms, so using it makes the fixture depend on whether the test
        // process was started in a UTF-8 locale. ZIP's language-encoding flag
        // defines these name bytes as UTF-8 and keeps the test host-independent.
        void writeUtf8ZipEntries(const fs::path& path, const std::vector<std::string>& entryPaths)
        {
            struct CentralEntry
            {
                std::string path;
                std::uint32_t crc;
                std::uint32_t size;
                std::uint32_t offset;
            };

            auto put16 = [](std::ostream& out, std::uint16_t value) {
                out.put(static_cast<char>(value & 0xff));
                out.put(static_cast<char>((value >> 8) & 0xff));
            };
            auto put32 = [](std::ostream& out, std::uint32_t value) {
                for (int shift = 0; shift < 32; shift += 8)
                    out.put(static_cast<char>((value >> shift) & 0xff));
            };
            auto crc32 = [](std::string_view bytes) {
                std::uint32_t crc = 0xffffffffu;
                for (const auto byte : bytes)
                {
                    crc ^= static_cast<unsigned char>(byte);
                    for (int bit = 0; bit < 8; ++bit)
                        crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
                }
                return ~crc;
            };

            std::ofstream out(path, std::ios::binary);
            REQUIRE(out);
            std::vector<CentralEntry> central;
            for (const auto& entryPath : entryPaths)
            {
                REQUIRE(entryPath.size() <= 0xffff);
                const auto offset = static_cast<std::uint32_t>(static_cast<std::streamoff>(out.tellp()));
                const auto crc = crc32(ArchiveBytes);
                put32(out, 0x04034b50); // Local file header
                put16(out, 20);
                put16(out, 0x0800); // UTF-8 names
                put16(out, 0); // Stored
                put16(out, 0); put16(out, 0);
                put32(out, crc);
                put32(out, static_cast<std::uint32_t>(ArchiveBytes.size()));
                put32(out, static_cast<std::uint32_t>(ArchiveBytes.size()));
                put16(out, static_cast<std::uint16_t>(entryPath.size()));
                put16(out, 0);
                out.write(entryPath.data(), static_cast<std::streamsize>(entryPath.size()));
                out.write(ArchiveBytes.data(), static_cast<std::streamsize>(ArchiveBytes.size()));
                central.push_back({entryPath, crc, static_cast<std::uint32_t>(ArchiveBytes.size()), offset});
            }

            const auto centralOffset = static_cast<std::uint32_t>(static_cast<std::streamoff>(out.tellp()));
            for (const auto& entry : central)
            {
                put32(out, 0x02014b50); // Central directory file header
                put16(out, static_cast<std::uint16_t>((3 << 8) | 20));
                put16(out, 20);
                put16(out, 0x0800);
                put16(out, 0);
                put16(out, 0); put16(out, 0);
                put32(out, entry.crc);
                put32(out, entry.size);
                put32(out, entry.size);
                put16(out, static_cast<std::uint16_t>(entry.path.size()));
                put16(out, 0); put16(out, 0);
                put16(out, 0); put16(out, 0);
                put32(out, 0100644u << 16);
                put32(out, entry.offset);
                out.write(entry.path.data(), static_cast<std::streamsize>(entry.path.size()));
            }
            const auto centralSize = static_cast<std::uint32_t>(static_cast<std::streamoff>(out.tellp())) - centralOffset;
            put32(out, 0x06054b50); // End of central directory
            put16(out, 0); put16(out, 0);
            put16(out, static_cast<std::uint16_t>(central.size()));
            put16(out, static_cast<std::uint16_t>(central.size()));
            put32(out, centralSize);
            put32(out, centralOffset);
            put16(out, 0);
            REQUIRE(out.good());
        }

        void writeGzipWrappedRawFile(const fs::path& path)
        {
            auto* writer = archive_write_new();
            REQUIRE(writer != nullptr);
            REQUIRE(archive_write_set_format_raw(writer) == ARCHIVE_OK);
            REQUIRE(archive_write_add_filter_gzip(writer) == ARCHIVE_OK);
#ifdef _WIN32
            REQUIRE(archive_write_open_filename_w(writer, path.c_str()) == ARCHIVE_OK);
#else
            REQUIRE(archive_write_open_filename(writer, path.c_str()) == ARCHIVE_OK);
#endif
            auto* entry = archive_entry_new();
            REQUIRE(entry != nullptr);
            archive_entry_set_pathname(entry, "payload.bin");
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
            archive_entry_set_size(entry, ArchiveBytes.size());
            REQUIRE(archive_write_header(writer, entry) == ARCHIVE_OK);
            REQUIRE(archive_write_data(writer, ArchiveBytes.data(), ArchiveBytes.size()) == static_cast<la_ssize_t>(ArchiveBytes.size()));
            archive_entry_free(entry);
            REQUIRE(archive_write_close(writer) == ARCHIVE_OK);
            REQUIRE(archive_write_free(writer) == ARCHIVE_OK);
        }

        std::vector<char> decodeUuFixture(const fs::path& path)
        {
            std::ifstream input(path);
            REQUIRE(input);
            std::vector<char> result;
            std::string line;
            bool inData = false;
            while (std::getline(input, line))
            {
                if (!inData)
                {
                    inData = line.starts_with("begin ");
                    continue;
                }
                if (line == "`" || line == "end") break;
                REQUIRE_FALSE(line.empty());
                auto remaining = static_cast<std::size_t>((static_cast<unsigned char>(line.front()) - 32) & 63);
                std::size_t offset = 1;
                while (remaining != 0)
                {
                    REQUIRE(offset + 3 < line.size());
                    const auto decode = [&](std::size_t index) {
                        return static_cast<unsigned char>((static_cast<unsigned char>(line[index]) - 32) & 63);
                    };
                    const auto first = decode(offset);
                    const auto second = decode(offset + 1);
                    const auto third = decode(offset + 2);
                    const auto fourth = decode(offset + 3);
                    const std::array<char, 3> decoded{
                        static_cast<char>((first << 2) | (second >> 4)),
                        static_cast<char>((second << 4) | (third >> 2)),
                        static_cast<char>((third << 6) | fourth),
                    };
                    const auto count = std::min<std::size_t>(remaining, decoded.size());
                    result.insert(result.end(), decoded.begin(), decoded.begin() + count);
                    remaining -= count;
                    offset += 4;
                }
            }
            REQUIRE(inData);
            return result;
        }
    }

    TEST_CASE("bundled archive backend inventories and extracts ZIP and 7z bytes", "[setup][archive]")
    {
        TempDirectory temp;
        for (const auto sevenZip : {false, true})
        {
            const auto archivePath = temp.path / (sevenZip ? "sample.7z" : "sample.zip");
            const auto staging = temp.path / (sevenZip ? "extract-7z" : "extract-zip");
            writeArchive(archivePath, sevenZip);
            const auto inventory = inspectArchive(archivePath);
            REQUIRE(inventory.entries.size() == 1);
            CHECK(inventory.entries[0].path == "payload/data.bin");
            CHECK(inventory.entries[0].size == ArchiveBytes.size());
            CHECK(inventory.totalBytes == ArchiveBytes.size());
            const auto extracted = extractArchive(archivePath, staging);
            CHECK(extracted.entries.size() == 1);
            std::ifstream input(staging / "payload/data.bin", std::ios::binary);
            const std::string actual((std::istreambuf_iterator<char>(input)), {});
            CHECK(actual == std::string(ArchiveBytes));
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

    TEST_CASE("archive cancellation cleans the original relative staging after the callback changes cwd", "[setup][archive]")
    {
        TempDirectory temp;
        CurrentPathGuard restoreCurrentPath;
        const auto original = temp.path / "original";
        const auto changed = temp.path / "changed";
        fs::create_directories(original);
        fs::create_directories(changed / "same-stage");
        std::ofstream(changed / "same-stage/keep.txt") << "keep";
        const auto archivePath = original / "sample.zip";
        writeArchive(archivePath, false);

        fs::current_path(original);
        CHECK_THROWS(extractArchive(archivePath, "same-stage", [&](std::uint64_t, std::uint64_t) {
            fs::current_path(changed);
            return false;
        }));

        CHECK_FALSE(fs::exists(original / "same-stage"));
        CHECK(fs::exists(changed / "same-stage/keep.txt"));
    }

    TEST_CASE("archive inventory rejects case-aliased destination components", "[setup][archive]")
    {
        TempDirectory temp;
        const auto asciiArchive = temp.path / "ascii-alias.zip";
        writeArchiveEntries(asciiArchive, false, {{"Payload/one.txt", false}, {"payload/two.txt", false}});
        CHECK_THROWS(inspectArchive(asciiArchive));

#ifdef _WIN32
        const auto unicodeArchive = temp.path / "unicode-alias.zip";
        writeUtf8ZipEntries(unicodeArchive,
            {"payload/\xC3\x84.txt", "payload/\xC3\xA4.txt"});
        CHECK_THROWS(inspectArchive(unicodeArchive));
#endif
    }

    TEST_CASE("archive backend preserves valid Unicode entry names and bytes", "[setup][archive]")
    {
        TempDirectory temp;
        const std::string entryPath = "payload/\xE2\x98\x83-\xC3\xA9.bin";
        const auto archivePath = temp.path / "unicode.zip";
        const auto staging = temp.path / "unicode";
        writeUtf8ZipEntries(archivePath, {entryPath});

        const auto inventory = inspectArchive(archivePath);
        REQUIRE(inventory.entries.size() == 1);
        CHECK(inventory.entries[0].path == entryPath);
        extractArchive(archivePath, staging);
        std::ifstream input(staging / fs::path(u8"payload/\u2603-\u00e9.bin"), std::ios::binary);
        const std::string actual((std::istreambuf_iterator<char>(input)), {});
        CHECK(actual == std::string(ArchiveBytes));
    }

#ifndef _WIN32
    TEST_CASE("archive UTF-8 names are locale-independent and keep locale thread-local", "[setup][archive]")
    {
        const auto* currentLocale = std::setlocale(LC_CTYPE, nullptr);
        REQUIRE(currentLocale != nullptr);
        const std::string originalLocale(currentLocale);
        struct LocaleRestore
        {
            std::string name;
            ~LocaleRestore() { std::setlocale(LC_CTYPE, name.c_str()); }
        } restore{originalLocale};
        REQUIRE(std::setlocale(LC_CTYPE, "C") != nullptr);

        TempDirectory temp;
        const std::string entryPath = "payload/\xE2\x98\x83-\xC3\xA9.bin";
        const auto archivePath = temp.path / "unicode-c-locale.zip";
        writeUtf8ZipEntries(archivePath, {entryPath});

        bool decoded = false;
        bool threadLocaleRestored = false;
        std::thread reader([&] {
            const auto before = uselocale(static_cast<locale_t>(nullptr));
            try
            {
                const auto inventory = inspectArchive(archivePath);
                decoded = inventory.entries.size() == 1 && inventory.entries[0].path == entryPath;
            }
            catch (...)
            {
                decoded = false;
            }
            threadLocaleRestored = uselocale(static_cast<locale_t>(nullptr)) == before;
        });
        reader.join();
        CHECK(decoded);
        CHECK(threadLocaleRestored);
        CHECK(std::string(std::setlocale(LC_CTYPE, nullptr)) == "C");

        const auto malformedArchive = temp.path / "malformed-utf8.zip";
        writeUtf8ZipEntries(malformedArchive, {std::string("payload/\xC3\x28.bin")});
        CHECK_THROWS(inspectArchive(malformedArchive));
        CHECK(std::string(std::setlocale(LC_CTYPE, nullptr)) == "C");
    }
#endif

    TEST_CASE("archive backend does not use PATH helpers and rejects outer compression wrappers", "[setup][archive]")
    {
        TempDirectory temp;
        const auto archivePath = temp.path / "without-path.zip";
        const auto staging = temp.path / "without-path";
        writeArchive(archivePath, false);

        EnvironmentVariableGuard restorePath("PATH");
        restorePath.clear();
        CHECK(inspectArchive(archivePath).entries.size() == 1);
        CHECK(extractArchive(archivePath, staging).entries.size() == 1);
        CHECK(fs::exists(staging / "payload/data.bin"));

        const auto wrapped = temp.path / "unsupported.gz";
        writeGzipWrappedRawFile(wrapped);
        CHECK_THROWS(inspectArchive(wrapped));
    }

    TEST_CASE("archive backend rejects Windows device aliases and control characters", "[setup][archive]")
    {
        TempDirectory temp;
        const std::vector<std::string> unsafePaths{
            "payload/COM\xC2\xB9.txt",
            "payload/lpt\xC2\xB2",
            "payload/control-\x1F.txt",
        };
        std::size_t index = 0;
        for (const auto& entryPath : unsafePaths)
        {
            const auto archivePath = temp.path / ("unsafe-" + std::to_string(index++) + ".zip");
            writeUtf8ZipEntries(archivePath, {entryPath});
            CHECK_THROWS(inspectArchive(archivePath));
        }
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
        CHECK(actual == std::string(expected));
    }

    TEST_CASE("bundled archive backend extracts the licensed legacy RAR fixture exactly", "[setup][archive]")
    {
        TempDirectory temp;
        const auto fixturePath = fs::path(RWE_SOURCE_DIR) / "tests/fixtures/archive-backend/rar4-subblock.rar.uu";
        const auto archivePath = temp.path / "legacy.rar";
        const auto decoded = decodeUuFixture(fixturePath);
        {
            std::ofstream output(archivePath, std::ios::binary);
            output.write(decoded.data(), static_cast<std::streamsize>(decoded.size()));
            REQUIRE(output);
        }

        constexpr auto expected = "test text document\r\n"sv;
        const auto inventory = inspectArchive(archivePath);
        REQUIRE(inventory.entries.size() == 1);
        CHECK(inventory.entries[0].path == "test.txt");
        CHECK(inventory.entries[0].size == expected.size());
        const auto staging = temp.path / "rar4";
        extractArchive(archivePath, staging);
        std::ifstream input(staging / "test.txt", std::ios::binary);
        const std::string actual((std::istreambuf_iterator<char>(input)), {});
        CHECK(actual == std::string(expected));
    }

    TEST_CASE("archive backend rejects traversal and symbolic links before leaving staged files", "[setup][archive]")
    {
        TempDirectory temp;
        for (const auto& [name, entryPath, symlink] : {
                 std::tuple{"traversal.zip", "../escape.txt", false},
                 std::tuple{"link.zip", "payload/link", true}})
        {
            const auto archivePath = temp.path / name;
            const auto staging = temp.path / (std::string(name) + ".staging");
            writeArchive(archivePath, false, entryPath, symlink);
            CHECK_THROWS(extractArchive(archivePath, staging));
            CHECK_FALSE(fs::exists(staging));
            CHECK_FALSE(fs::exists(temp.path / "escape.txt"));
        }
    }
}
