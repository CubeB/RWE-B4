#include "ArchiveBackend.h"

#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace rwe
{
    namespace
    {
        constexpr std::uint64_t MaxEntries = 100000;
        constexpr std::uint64_t MaxExpandedBytes = 16ull * 1024 * 1024 * 1024;

        struct Reader
        {
            archive* value = archive_read_new();
            Reader() = default;
            Reader(const Reader&) = delete;
            Reader& operator=(const Reader&) = delete;
            Reader(Reader&& other) noexcept : value(other.value) { other.value = nullptr; }
            Reader& operator=(Reader&& other) noexcept
            {
                if (this != &other) { if (value) archive_read_free(value); value = other.value; other.value = nullptr; }
                return *this;
            }
            ~Reader() { if (value) archive_read_free(value); }
        };

        std::string archiveError(archive* reader, const char* operation)
        {
            const auto* detail = archive_error_string(reader);
            return std::string(operation) + (detail ? ": " + std::string(detail) : "");
        }

        std::string lowerAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
            });
            return value;
        }

        fs::path checkedRelativePath(const std::string& value)
        {
            if (value.empty() || value.front() == '/' || value.find_first_of("\\:\0") != std::string::npos)
                throw std::runtime_error("Archive contains an absolute or invalid path");
            fs::path result;
            std::size_t start = 0;
            while (start <= value.size())
            {
                const auto end = value.find('/', start);
                const auto part = value.substr(start, end == std::string::npos ? end : end - start);
                if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' '
                    || part.find_first_of("<>\"|?*") != std::string::npos)
                    throw std::runtime_error("Archive contains an unsafe path component");
                const auto dot = part.find('.');
                const auto device = lowerAscii(part.substr(0, dot));
                if (device == "con" || device == "prn" || device == "aux" || device == "nul"
                    || (device.size() == 4 && (device.compare(0, 3, "com") == 0 || device.compare(0, 3, "lpt") == 0)
                        && device[3] >= '1' && device[3] <= '9'))
                    throw std::runtime_error("Archive contains a reserved Windows path");
                result /= fs::u8path(part);
                if (end == std::string::npos) break;
                start = end + 1;
            }
            return result;
        }

        Reader openReader(const fs::path& path)
        {
            Reader result;
            if (!result.value) throw std::runtime_error("Cannot allocate archive reader");
            archive_read_support_filter_all(result.value);
            archive_read_support_format_zip(result.value);
            archive_read_support_format_rar(result.value);
            archive_read_support_format_rar5(result.value);
            archive_read_support_format_7zip(result.value);
#ifdef _WIN32
            if (archive_read_open_filename_w(result.value, path.c_str(), 64 * 1024) != ARCHIVE_OK)
#else
            if (archive_read_open_filename(result.value, path.c_str(), 64 * 1024) != ARCHIVE_OK)
#endif
                throw std::runtime_error(archiveError(result.value, "Cannot open archive"));
            return result;
        }

        ArchiveInventory inventory(archive* reader)
        {
            ArchiveInventory result;
            std::set<std::string> names;
            archive_entry* entry = nullptr;
            int status;
            while ((status = archive_read_next_header(reader, &entry)) == ARCHIVE_OK)
            {
                if (result.entries.size() >= MaxEntries) throw std::runtime_error("Archive entry limit exceeded");
                if (archive_entry_is_encrypted(entry) > 0) throw std::runtime_error("Encrypted archive entries are unsupported");
                if (archive_entry_symlink(entry) || archive_entry_hardlink(entry))
                    throw std::runtime_error("Archive links are unsupported");
                const auto* rawPath = archive_entry_pathname_utf8(entry);
                if (!rawPath) rawPath = archive_entry_pathname(entry);
                if (!rawPath) throw std::runtime_error("Archive entry has no path");
                auto relative = checkedRelativePath(rawPath);
                auto canonical = lowerAscii(relative.generic_string());
                if (!names.insert(canonical).second) throw std::runtime_error("Archive has colliding paths");
                const auto type = archive_entry_filetype(entry);
                if (type != AE_IFREG && type != AE_IFDIR) throw std::runtime_error("Archive contains a special file");
                const auto signedSize = archive_entry_size(entry);
                if (type == AE_IFREG && (!archive_entry_size_is_set(entry) || signedSize < 0))
                    throw std::runtime_error("Archive has an invalid file size");
                const auto size = type == AE_IFDIR ? 0 : static_cast<std::uint64_t>(signedSize);
                if (size > MaxExpandedBytes - result.totalBytes) throw std::runtime_error("Archive expansion limit exceeded");
                result.totalBytes += size;
                result.entries.push_back({relative.generic_string(), type == AE_IFDIR ? ArchiveEntryKind::directory : ArchiveEntryKind::regularFile, size});
                if (archive_read_data_skip(reader) < ARCHIVE_WARN) throw std::runtime_error(archiveError(reader, "Cannot skip archive entry"));
            }
            if (status != ARCHIVE_EOF) throw std::runtime_error(archiveError(reader, "Cannot read archive inventory"));
            const auto* name = archive_format_name(reader);
            result.format = name ? name : "unknown";
            return result;
        }
    }

    ArchiveInventory inspectArchive(const fs::path& archivePath)
    {
        auto reader = openReader(archivePath);
        return inventory(reader.value);
    }

    ArchiveInventory extractArchive(const fs::path& archivePath, const fs::path& stagingDirectory,
        const ArchiveProgress& progress)
    {
        std::error_code error;
        if (!fs::create_directory(stagingDirectory, error))
            throw std::runtime_error(error ? "Cannot create fresh staging directory: " + error.message()
                                           : "Staging directory already exists");
        try
        {
            auto reader = openReader(archivePath);
            auto result = inventory(reader.value);
            reader = openReader(archivePath);
            archive_entry* entry = nullptr;
            std::uint64_t completed = 0;
            std::array<char, 64 * 1024> buffer{};
            int status;
            while ((status = archive_read_next_header(reader.value, &entry)) == ARCHIVE_OK)
            {
                const auto* rawPath = archive_entry_pathname_utf8(entry);
                if (!rawPath) rawPath = archive_entry_pathname(entry);
                const auto relative = checkedRelativePath(rawPath ? rawPath : "");
                const auto destination = stagingDirectory / relative;
                if (archive_entry_filetype(entry) == AE_IFDIR)
                {
                    fs::create_directories(destination);
                    continue;
                }
                fs::create_directories(destination.parent_path());
                std::ofstream output(destination, std::ios::binary | std::ios::out);
                if (!output) throw std::runtime_error("Cannot create extracted file");
                std::uint64_t written = 0;
                la_ssize_t count;
                while ((count = archive_read_data(reader.value, buffer.data(), buffer.size())) > 0)
                {
                    if (progress && !progress(completed + written, result.totalBytes))
                        throw std::runtime_error("Archive extraction cancelled");
                    if (static_cast<std::uint64_t>(count) > MaxExpandedBytes - completed - written)
                        throw std::runtime_error("Archive expansion limit exceeded");
                    output.write(buffer.data(), count);
                    if (!output) throw std::runtime_error("Cannot write extracted file");
                    written += static_cast<std::uint64_t>(count);
                }
                if (count < 0) throw std::runtime_error(archiveError(reader.value, "Cannot extract archive entry"));
                if (written != static_cast<std::uint64_t>(archive_entry_size(entry)))
                    throw std::runtime_error("Extracted file size does not match archive inventory");
                output.close();
                if (!output) throw std::runtime_error("Cannot finish extracted file");
                completed += written;
            }
            if (status != ARCHIVE_EOF) throw std::runtime_error(archiveError(reader.value, "Cannot extract archive"));
            if (progress && !progress(completed, result.totalBytes)) throw std::runtime_error("Archive extraction cancelled");
            return result;
        }
        catch (...)
        {
            std::error_code ignored;
            fs::remove_all(stagingDirectory, ignored);
            throw;
        }
    }
}
