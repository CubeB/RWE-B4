#pragma once

#include <filesystem>
#include <nlohmann/json_fwd.hpp>

namespace rwe
{
    // PACKAGE-001/002/003, docs/PACKAGE-LIBRARY-FINDINGS.md. RWE-original;
    // inspection and registry storage do not establish gameplay compatibility.
    nlohmann::json emptyPackageRegistry();
    void validatePackageRegistry(const nlohmann::json& registry);
    nlohmann::json loadPackageRegistry(const std::filesystem::path& filename);
    void savePackageRegistry(const std::filesystem::path& filename, const nlohmann::json& registry);
    nlohmann::json inspectContentPackage(const std::filesystem::path& source);
}
