#pragma once

#include <filesystem>
#include <functional>
#include <nlohmann/json_fwd.hpp>

namespace rwe
{
    // PACKAGE-001/002/003, docs/PACKAGE-LIBRARY-FINDINGS.md. RWE-original;
    // inspection and registry storage do not establish gameplay compatibility.
    nlohmann::json emptyPackageRegistry();
    void validatePackageRegistry(const nlohmann::json& registry);
    nlohmann::json loadPackageRegistry(const std::filesystem::path& filename);
    void savePackageRegistry(const std::filesystem::path& filename, const nlohmann::json& registry);
    // Holds the cross-process registry lock across read, mutation, validation
    // and atomic replacement so concurrent updates cannot lose one another.
    nlohmann::json updatePackageRegistry(
        const std::filesystem::path& filename,
        const std::function<void(nlohmann::json&)>& mutation);
    nlohmann::json inspectContentPackage(const std::filesystem::path& source);
}
