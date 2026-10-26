#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace rwe
{
    enum class ArchiveEntryKind { regularFile, directory };

    struct ArchiveItem
    {
        std::string path;
        ArchiveEntryKind kind;
        std::uint64_t size;
    };

    struct ArchiveInventory
    {
        std::string format;
        std::vector<ArchiveItem> entries;
        std::uint64_t totalBytes = 0;
    };

    using ArchiveProgress = std::function<bool(std::uint64_t completed, std::uint64_t total)>;

    // PACKAGE-007. Extraction only succeeds into a newly-created, empty staging
    // directory. It never follows archive paths, writes links, or runs files.
    ArchiveInventory inspectArchive(const std::filesystem::path& archivePath);
    ArchiveInventory extractArchive(const std::filesystem::path& archivePath,
        const std::filesystem::path& newStagingDirectory,
        const ArchiveProgress& progress = {});
}
