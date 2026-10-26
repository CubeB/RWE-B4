#pragma once

#include <filesystem>
#include <nlohmann/json_fwd.hpp>

namespace rwe
{
    struct PackageLibraryPaths
    {
        std::filesystem::path root;
        std::filesystem::path registry;
        std::filesystem::path packages;
        std::filesystem::path mods;
        std::filesystem::path mapPacks;
        std::filesystem::path staging;
    };

    // PACKAGE-005: shared per-user roots. Only packages/ is registry-owned;
    // legacy mods and MapPacks directories remain external discovery locations.
    PackageLibraryPaths packageLibraryPaths(const std::filesystem::path& userDataPath);
    // PACKAGE-001/002/003, docs/PACKAGE-LIBRARY-FINDINGS.md. RWE-original;
    // inspection and registry storage do not establish gameplay compatibility.
    nlohmann::json emptyPackageRegistry();
    void validatePackageRegistry(const nlohmann::json& registry);
    nlohmann::json loadPackageRegistry(const std::filesystem::path& filename);
    void savePackageRegistry(const std::filesystem::path& filename, const nlohmann::json& registry);
    nlohmann::json inspectContentPackage(const std::filesystem::path& source);
}
