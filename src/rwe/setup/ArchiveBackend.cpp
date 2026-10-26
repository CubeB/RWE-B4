#include "ArchiveBackend.h"

#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <locale.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace rwe
{
    namespace
    {
        constexpr std::uint64_t MaxEntries = 100000;
        constexpr std::uint64_t MaxExpandedBytes = 16ull * 1024 * 1024 * 1024;
        constexpr std::size_t MaxPathBytes = 32767;

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

#ifndef _WIN32
        locale_t utf8ArchiveLocale()
        {
            static const auto locale = [] {
                constexpr const char* names[] = {"C.UTF-8", "C.utf8", "en_US.UTF-8", "UTF-8"};
                for (const auto* name : names)
                    if (const auto result = newlocale(LC_CTYPE_MASK, name, static_cast<locale_t>(nullptr)))
                        return result;
                throw std::runtime_error("A UTF-8 locale is required to read archive paths");
            }();
            return locale;
        }

        class ArchiveUtf8Locale
        {
        public:
            ArchiveUtf8Locale() : previous(uselocale(utf8ArchiveLocale()))
            {
                if (!previous) throw std::system_error(errno, std::generic_category(), "Cannot set archive UTF-8 locale");
            }
            ~ArchiveUtf8Locale() { uselocale(previous); }

            ArchiveUtf8Locale(const ArchiveUtf8Locale&) = delete;
            ArchiveUtf8Locale& operator=(const ArchiveUtf8Locale&) = delete;

        private:
            locale_t previous;
        };
#else
        class ArchiveUtf8Locale
        {
        };
#endif

        struct ArchiveHeader
        {
            int status;
            std::string path;
            bool hasUtf8Path;
            decltype(archive_entry_filetype(static_cast<archive_entry*>(nullptr))) type;
        };

        ArchiveHeader readUtf8Header(archive* reader, archive_entry** entry)
        {
            [[maybe_unused]] ArchiveUtf8Locale locale;
            const auto status = archive_read_next_header(reader, entry);
            if (status != ARCHIVE_OK) return {status, {}, false, 0};
            const auto* pathname = archive_entry_pathname_utf8(*entry);
            return {status, pathname ? pathname : "", pathname != nullptr, archive_entry_filetype(*entry)};
        }

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

        bool reservedPortDevice(std::string_view device, std::string_view prefix)
        {
            if (!device.starts_with(prefix)) return false;
            const auto suffix = device.substr(prefix.size());
            return (suffix.size() == 1 && suffix.front() >= '1' && suffix.front() <= '9')
                || suffix == "\xC2\xB9" || suffix == "\xC2\xB2" || suffix == "\xC2\xB3";
        }

        fs::path pathFromUtf8(std::string_view value)
        {
            const auto* first = reinterpret_cast<const char8_t*>(value.data());
            return fs::path(std::u8string(first, first + value.size()));
        }

        std::string pathToUtf8(const fs::path& value)
        {
            const auto bytes = value.generic_u8string();
            return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }

        struct PathLess
        {
            bool operator()(const fs::path& left, const fs::path& right) const
            {
#ifdef _WIN32
                const auto& leftNative = left.native();
                const auto& rightNative = right.native();
                return CompareStringOrdinal(leftNative.data(), static_cast<int>(leftNative.size()),
                           rightNative.data(), static_cast<int>(rightNative.size()), TRUE)
                    == CSTR_LESS_THAN;
#else
                return lowerAscii(pathToUtf8(left)) < lowerAscii(pathToUtf8(right));
#endif
            }
        };

        fs::path checkedRelativePath(const std::string& value)
        {
            if (value.empty() || value.size() > MaxPathBytes || value.front() == '/'
                || value.find('\\') != std::string::npos || value.find(':') != std::string::npos
                || value.find('\0') != std::string::npos)
                throw std::runtime_error("Archive contains an absolute or invalid path");
            fs::path result;
            std::size_t start = 0;
            while (start <= value.size())
            {
                const auto end = value.find('/', start);
                const auto part = value.substr(start, end == std::string::npos ? end : end - start);
                if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' '
                    || part.find_first_of("<>\"|?*") != std::string::npos
                    || std::any_of(part.begin(), part.end(), [](unsigned char byte) { return byte < 32; }))
                    throw std::runtime_error("Archive contains an unsafe path component");
                const auto dot = part.find('.');
                const auto device = lowerAscii(part.substr(0, dot));
                if (device == "con" || device == "prn" || device == "aux" || device == "nul"
                    || reservedPortDevice(device, "com") || reservedPortDevice(device, "lpt"))
                    throw std::runtime_error("Archive contains a reserved Windows path");
                result /= pathFromUtf8(part);
                if (end == std::string::npos) break;
                start = end + 1;
            }
            return result;
        }

        void claimDestination(const fs::path& relative, ArchiveEntryKind kind,
            std::set<fs::path, PathLess>& explicitPaths,
            std::map<fs::path, ArchiveEntryKind, PathLess>& destinations)
        {
            if (!explicitPaths.insert(relative).second)
                throw std::runtime_error("Archive has colliding paths");

            fs::path prefix;
            for (auto part = relative.begin(); part != relative.end(); ++part)
            {
                prefix /= *part;
                const auto requiredKind = std::next(part) == relative.end() ? kind : ArchiveEntryKind::directory;
                const auto [existing, inserted] = destinations.emplace(prefix, requiredKind);
                if (!inserted && (existing->first != prefix || existing->second != requiredKind))
                    throw std::runtime_error("Archive has colliding paths");
            }
        }

        fs::path stableExistingPath(const fs::path& path, const char* description)
        {
            std::error_code error;
            const auto result = fs::canonical(path, error);
            if (error) throw std::runtime_error(std::string("Cannot resolve ") + description + ": " + error.message());
            return result;
        }

        fs::path stableNewPath(const fs::path& path)
        {
            std::error_code error;
            const auto absolute = fs::absolute(path, error);
            if (error) throw std::runtime_error("Cannot resolve staging directory: " + error.message());
            const auto result = fs::weakly_canonical(absolute, error);
            if (error) throw std::runtime_error("Cannot resolve staging directory: " + error.message());
            return result;
        }

        class ExclusiveOutput
        {
        public:
            explicit ExclusiveOutput(const fs::path& path)
            {
#ifdef _WIN32
                handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle == INVALID_HANDLE_VALUE)
                    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Cannot create extracted file");
#else
                descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL
#ifdef O_CLOEXEC
                        | O_CLOEXEC
#endif
#ifdef O_NOFOLLOW
                        | O_NOFOLLOW
#endif
                    , 0600);
                if (descriptor < 0) throw std::system_error(errno, std::generic_category(), "Cannot create extracted file");
#endif
            }

            ExclusiveOutput(const ExclusiveOutput&) = delete;
            ExclusiveOutput& operator=(const ExclusiveOutput&) = delete;

            ~ExclusiveOutput()
            {
#ifdef _WIN32
                if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
#else
                if (descriptor >= 0) ::close(descriptor);
#endif
            }

            void write(const char* data, std::size_t size)
            {
                while (size != 0)
                {
#ifdef _WIN32
                    DWORD written = 0;
                    if (!WriteFile(handle, data, static_cast<DWORD>(size), &written, nullptr) || written == 0)
                        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Cannot write extracted file");
#else
                    const auto written = ::write(descriptor, data, size);
                    if (written < 0 && errno == EINTR) continue;
                    if (written <= 0) throw std::system_error(errno, std::generic_category(), "Cannot write extracted file");
#endif
                    data += written;
                    size -= written;
                }
            }

            void close()
            {
#ifdef _WIN32
                const auto openHandle = handle;
                handle = INVALID_HANDLE_VALUE;
                if (!CloseHandle(openHandle))
                    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Cannot finish extracted file");
#else
                const auto openDescriptor = descriptor;
                descriptor = -1;
                if (::close(openDescriptor) != 0)
                    throw std::system_error(errno, std::generic_category(), "Cannot finish extracted file");
#endif
            }

        private:
#ifdef _WIN32
            HANDLE handle = INVALID_HANDLE_VALUE;
#else
            int descriptor = -1;
#endif
        };

        void ensureDirectories(const fs::path& root, const fs::path& relative,
            std::set<fs::path, PathLess>& ownedDirectories)
        {
            auto current = root;
            for (const auto& part : relative)
            {
                current /= part;
                std::error_code error;
                const auto status = fs::symlink_status(current, error);
                if (!error && status.type() != fs::file_type::not_found)
                {
                    if (fs::is_symlink(status) || !fs::is_directory(status)
                        || ownedDirectories.find(current) == ownedDirectories.end())
                        throw std::runtime_error("Archive destination collides with the filesystem");
                    continue;
                }
                if (error && error != std::errc::no_such_file_or_directory)
                    throw std::runtime_error("Cannot inspect extracted directory: " + error.message());
                error.clear();
                if (!fs::create_directory(current, error))
                    throw std::runtime_error(error ? "Cannot create extracted directory: " + error.message()
                                                   : "Archive destination collides with the filesystem");
                ownedDirectories.insert(current);
            }
        }

        void removeOwnedDirectory(const fs::path& ownedRoot) noexcept
        {
            std::error_code error;
            const auto status = fs::symlink_status(ownedRoot, error);
            if (error || fs::is_symlink(status) || !fs::is_directory(status)) return;
            const auto resolved = fs::canonical(ownedRoot, error);
            if (error || resolved != ownedRoot) return;
            fs::remove_all(ownedRoot, error);
        }

        Reader openReader(const fs::path& path)
        {
            Reader result;
            if (!result.value) throw std::runtime_error("Cannot allocate archive reader");
            const auto support = [&](int status, const char* feature) {
                if (status != ARCHIVE_OK)
                    throw std::runtime_error(archiveError(result.value, feature));
            };
            // PACKAGE-007 permits only container codecs implemented in this
            // process. In particular, support_filter_all registers filters
            // such as grzip that launch helper programs from PATH.
            support(archive_read_support_filter_none(result.value), "Cannot register unfiltered archives");
            support(archive_read_support_format_zip(result.value), "Cannot register ZIP archives");
            support(archive_read_support_format_rar(result.value), "Cannot register RAR archives");
            support(archive_read_support_format_rar5(result.value), "Cannot register RAR5 archives");
            support(archive_read_support_format_7zip(result.value), "Cannot register 7z archives");
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
            std::set<fs::path, PathLess> explicitPaths;
            std::map<fs::path, ArchiveEntryKind, PathLess> destinations;
            archive_entry* entry = nullptr;
            int status;
            while (true)
            {
                const auto header = readUtf8Header(reader, &entry);
                status = header.status;
                if (status != ARCHIVE_OK) break;
                if (result.entries.size() >= MaxEntries) throw std::runtime_error("Archive entry limit exceeded");
                if (archive_entry_is_encrypted(entry) > 0) throw std::runtime_error("Encrypted archive entries are unsupported");
                if (archive_entry_symlink(entry) || archive_entry_hardlink(entry))
                    throw std::runtime_error("Archive links are unsupported");
                if (!header.hasUtf8Path) throw std::runtime_error("Archive entry path is missing or is not valid UTF-8");
                const auto type = header.type;
                std::string normalizedPath = header.path;
                if (type == AE_IFDIR && !normalizedPath.empty() && normalizedPath.back() == '/') normalizedPath.pop_back();
                auto relative = checkedRelativePath(normalizedPath);
                if (type != AE_IFREG && type != AE_IFDIR) throw std::runtime_error("Archive contains a special file");
                const auto kind = type == AE_IFDIR ? ArchiveEntryKind::directory : ArchiveEntryKind::regularFile;
                claimDestination(relative, kind, explicitPaths, destinations);
                const auto signedSize = archive_entry_size(entry);
                if (type == AE_IFREG && (!archive_entry_size_is_set(entry) || signedSize < 0))
                    throw std::runtime_error("Archive has an invalid file size");
                const auto size = type == AE_IFDIR ? 0 : static_cast<std::uint64_t>(signedSize);
                if (size > MaxExpandedBytes - result.totalBytes) throw std::runtime_error("Archive expansion limit exceeded");
                result.totalBytes += size;
                result.entries.push_back({pathToUtf8(relative), kind, size});
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
        auto reader = openReader(stableExistingPath(archivePath, "archive"));
        return inventory(reader.value);
    }

    ArchiveInventory extractArchive(const fs::path& archivePath, const fs::path& stagingDirectory,
        const ArchiveProgress& progress)
    {
        const auto stableArchive = stableExistingPath(archivePath, "archive");
        const auto stableStaging = stableNewPath(stagingDirectory);
        std::error_code error;
        if (!fs::create_directory(stableStaging, error))
            throw std::runtime_error(error ? "Cannot create fresh staging directory: " + error.message()
                                           : "Staging directory already exists");
        const auto ownedStaging = fs::canonical(stableStaging, error);
        if (error)
        {
            removeOwnedDirectory(stableStaging);
            throw std::runtime_error("Cannot verify fresh staging directory: " + error.message());
        }
        try
        {
            auto reader = openReader(stableArchive);
            auto result = inventory(reader.value);
            reader = openReader(stableArchive);
            archive_entry* entry = nullptr;
            std::uint64_t completed = 0;
            std::size_t entryIndex = 0;
            std::set<fs::path, PathLess> ownedDirectories{ownedStaging};
            std::array<char, 64 * 1024> buffer{};
            int status;
            while (true)
            {
                const auto header = readUtf8Header(reader.value, &entry);
                status = header.status;
                if (status != ARCHIVE_OK) break;
                if (entryIndex >= result.entries.size() || archive_entry_is_encrypted(entry) > 0
                    || archive_entry_symlink(entry) || archive_entry_hardlink(entry))
                    throw std::runtime_error("Archive changed after inventory");
                if (!header.hasUtf8Path) throw std::runtime_error("Archive entry path is missing or is not valid UTF-8");
                std::string normalizedPath = header.path;
                const auto type = header.type;
                if (type == AE_IFDIR && !normalizedPath.empty() && normalizedPath.back() == '/') normalizedPath.pop_back();
                const auto relative = checkedRelativePath(normalizedPath);
                if (type != AE_IFREG && type != AE_IFDIR)
                    throw std::runtime_error("Archive changed after inventory");
                const auto kind = type == AE_IFDIR ? ArchiveEntryKind::directory : ArchiveEntryKind::regularFile;
                if (kind == ArchiveEntryKind::regularFile && !archive_entry_size_is_set(entry))
                    throw std::runtime_error("Archive changed after inventory");
                const auto size = kind == ArchiveEntryKind::directory ? 0 : archive_entry_size(entry);
                const auto& expected = result.entries[entryIndex++];
                if (expected.path != pathToUtf8(relative) || expected.kind != kind
                    || size < 0 || expected.size != static_cast<std::uint64_t>(size))
                    throw std::runtime_error("Archive changed after inventory");
                const auto destination = ownedStaging / relative;
                if (type == AE_IFDIR)
                {
                    ensureDirectories(ownedStaging, relative, ownedDirectories);
                    continue;
                }
                ensureDirectories(ownedStaging, relative.parent_path(), ownedDirectories);
                ExclusiveOutput output(destination);
                std::uint64_t written = 0;
                la_ssize_t count;
                while ((count = archive_read_data(reader.value, buffer.data(), buffer.size())) > 0)
                {
                    if (progress && !progress(completed + written, result.totalBytes))
                        throw std::runtime_error("Archive extraction cancelled");
                    if (static_cast<std::uint64_t>(count) > MaxExpandedBytes - completed - written)
                        throw std::runtime_error("Archive expansion limit exceeded");
                    output.write(buffer.data(), static_cast<std::size_t>(count));
                    written += static_cast<std::uint64_t>(count);
                }
                if (count < 0) throw std::runtime_error(archiveError(reader.value, "Cannot extract archive entry"));
                if (written != static_cast<std::uint64_t>(archive_entry_size(entry)))
                    throw std::runtime_error("Extracted file size does not match archive inventory");
                output.close();
                completed += written;
            }
            if (status != ARCHIVE_EOF) throw std::runtime_error(archiveError(reader.value, "Cannot extract archive"));
            if (entryIndex != result.entries.size()) throw std::runtime_error("Archive changed after inventory");
            if (progress && !progress(completed, result.totalBytes)) throw std::runtime_error("Archive extraction cancelled");
            return result;
        }
        catch (...)
        {
            removeOwnedDirectory(ownedStaging);
            throw;
        }
    }
}
